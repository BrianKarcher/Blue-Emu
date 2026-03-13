#include "NesPpu.h"
#include <string>
#include <cstdint>
#include <WinUser.h>
#include "NesBus.h"
#include "Core.h"
#include "RendererLoopy.h"
#include "A12Mapper.h"
#include "MapperBase.h"
#include "Serializer.h"
#include "DebuggerContext.h"
#include "Mapper.h"
#include <array>
#include "Cartridge.h"

HWND m_hwnd;

NesPpu::NesPpu(SharedContext& ctx, Nes& nes) : context(ctx), nes(nes) {
	dbgContext = ctx.debugger_context;
	oam.fill(0xFF);
	m_ppuCtrl = 0;
	oamAddr = 0;
}

NesPpu::~NesPpu()
{
	if (renderer) {
		delete renderer;
		renderer = nullptr;
	}
}

void NesPpu::initialize() {
	renderer = new RendererLoopy(context);
	renderer->initialize(this);
}

void NesPpu::set_hwnd(HWND hwnd) {
	m_hwnd = hwnd;
}

void NesPpu::reset()
{
	m_ppuMask = 0;
	m_ppuCtrl = 0;
	m_ppuStatus = 0;
	ppuDataBuffer = 0;
	paletteTable.fill(0x00);
	renderer->reset();
	clearBuffer(context.GetBackBuffer());
	context.SwapBuffers();
	clearBuffer(context.GetBackBuffer());
}

void NesPpu::clearBuffer(uint32_t* buffer) {
	for (int i = 0; i < 256 * 240; i++) {
		buffer[i] = 0x00000000; // Black
	}
}

void NesPpu::step()
{
	// Emulate one NesPpu cycle here

}

uint8_t NesPpu::read(uint16_t address) {
	return read_register(0x2000 + (address & 0x7));
}

uint8_t NesPpu::peek(uint16_t address) {
	return peek_register(0x2000 + (address & 0x7));
}

void NesPpu::register_memory(NesBus& bus) {
	// TODO : Implement NesPpu open bus.
	bus.ReadRegisterAdd(0x2000, 0x3FFF, this);
	bus.ReadRegisterAdd(0x4014, 0x4014, this);
	bus.WriteRegisterAdd(0x2000, 0x3FFF, this);
	bus.WriteRegisterAdd(0x4014, 0x4014, this);
}

void NesPpu::writeOAM(uint16_t addr, uint8_t val) {
	oam[addr] = val;
}

void NesPpu::performDMA(uint8_t page)
{
	nes.dmaActive = true;
	nes.dmaPage = page;
	nes.dmaAddr = 0;
	// Timing penalty (513 or 514 CPU cycles)
	int extraCycle = (bus->cpu.GetCycleCount() & 1) ? 1 : 0;
	nes.dmaCycles = 513 + extraCycle;
}

void NesPpu::write(uint16_t address, uint8_t value) {
	if (address == 0x4014) {
		performDMA(value);
	}
	else {
		write_register(0x2000 + (address & 0x7), value);
	}
}

inline void NesPpu::write_register(uint16_t addr, uint8_t value)
{
	// addr is in the range 0x2000 to 0x2007
	// It is the CPU that writes to these registers
	// addr is mirrored every 8 bytes up to 0x3FFF so we mask it
	switch (addr) {
	case NesPpuCTRL:
		LOG(L"(%d) 0x%04X NesPpuCTRL Write 0x%02X\n", bus->cpu.GetCycleCount(), bus->cpu.GetPC(), value);
		m_ppuCtrl = value;
		//OutputDebugStringW((L"NesPpuCTRL: " + std::to_wstring(value) + L"\n").c_str());
		renderer->setNesPpuCTRL(value);
		break;
	case NesPpuMASK: // NesPpuMASK
		LOG(L"(%d) 0x%04X NesPpuMASK Write 0x%02X\n", bus->cpu.GetCycleCount(), bus->cpu.GetPC(), value);
		m_ppuMask = value;
		renderer->setNesPpuMask(value);
		break;
	case NesPpuSTATUS: // NesPpuSTATUS (read-only)
		LOG(L"(%d) 0x%04X NesPpuSTATUS Write 0x%02X\n", bus->cpu.GetCycleCount(), bus->cpu.GetPC(), value);
		// Ignore writes to NesPpuSTATUS
		break;
	case OAMADDR: // OAMADDR
		LOG(L"(%d) 0x%04X OAMADDR Write 0x%02X\n", bus->cpu.GetCycleCount(), bus->cpu.GetPC(), value);
		oamAddr = value;
		break;
	case OAMDATA:
		LOG(L"(%d) 0x%04X OAMDATA Write 0x%02X\n", bus->cpu.GetCycleCount(), bus->cpu.GetPC(), value);
		oam[oamAddr++] = value;
		break;
	case NesPpuSCROLL:
		LOG(L"(%d) 0x%04X NesPpuSCROLL Write 0x%02X\n", bus->cpu.GetCycleCount(), bus->cpu.GetPC(), value);
		renderer->writeScroll(value);
		break;
	case NesPpuADDR: // NesPpuADDR
		LOG(L"(%d) 0x%04X NesPpuADDR Write 0x%02X\n", bus->cpu.GetCycleCount(), bus->cpu.GetPC(), value);
		if (value == 0x3F) {
			int i = 0;
		}
		renderer->ppuWriteAddr(value);
		break;
	case NesPpuDATA: // NesPpuDATA
		LOG(L"(%d) 0x%04X NesPpuDATA Write 0x%02X\n", bus->cpu.GetCycleCount(), bus->cpu.GetPC(), value);
		uint16_t vramAddr = renderer->getNesPpuAddr();
		if (vramAddr >= 0x3F00) {
			int i = 0;
		}
		renderer->ppuIncrementVramAddr(m_ppuCtrl & NesPpuCTRL_INCREMENT ? 32 : 1);
		write_vram(vramAddr, value);
		if (m_mapper) {
			m_mapper->ClockIRQCounter(renderer->ppuGetVramAddr());
		}
		break;
	}
}

void NesPpu::SetVRAMAddress(uint16_t addr) {
	renderer->ppuWriteAddr(addr >> 8);
	renderer->ppuWriteAddr(addr);
	//vramAddr = addr & 0x3FFF;
}

inline uint8_t NesPpu::read_register(uint16_t addr)
{
	switch (addr)
	{
	case NesPpuCTRL:
	{
		LOG(L"(%d) 0x%04X NesPpuCTRL Read 0x%02X\n", bus->cpu.GetCycleCount(), bus->cpu.GetPC(), m_ppuCtrl);
		return m_ppuCtrl;
	}
	case NesPpuMASK:
	{
		LOG(L"(%d) 0x%04X NesPpuMASK Read\n", bus->cpu.GetCycleCount(), bus->cpu.GetPC());
		// not typically readable, return 0
		return 0;
	}
	case NesPpuSTATUS:
	{
		renderer->ppuReadStatus(); // Reset write toggle on reading NesPpuSTATUS
		// Return NesPpu status register value and clear VBlank flag
		uint8_t status = m_ppuStatus;
		LOG(L"(%d) 0x%04X NesPpuSTATUS Read 0x%02X\n", bus->cpu.GetCycleCount(), bus->cpu.GetPC(), status);
		m_ppuStatus &= ~NesPpuSTATUS_VBLANK;
		return status;
	}
	case OAMADDR:
	{
		LOG(L"(%d) 0x%04X OAMADDR Read 0x%02X\n", bus->cpu.GetCycleCount(), bus->cpu.GetPC(), oamAddr);
		return oamAddr;
	}
	case OAMDATA:
	{
		LOG(L"(%d) 0x%04X OAMDATA Read 0x%02X\n", bus->cpu.GetCycleCount(), bus->cpu.GetPC(), oam[oamAddr]);
		// Return OAM data at current OAMADDR
		return oam[oamAddr];
	}
	case NesPpuSCROLL:
	{
		LOG(L"(%d) 0x%04X NesPpuSCROLL Read\n", bus->cpu.GetCycleCount(), bus->cpu.GetPC());
		// NesPpuSCROLL is write-only, return 0
		return 0;
	}
	case NesPpuADDR:
	{
		LOG(L"(%d) 0x%04X NesPpuADDR Read\n", bus->cpu.GetCycleCount(), bus->cpu.GetPC());
		// NesPpuADDR is write-only, return 0
		return 0;
	}
	case NesPpuDATA:
	{
		// Read from VRAM at current vramAddr
		uint16_t vramAddr = renderer->getNesPpuAddr();
		uint8_t value = 0;
		if (vramAddr < 0x3F00) {
			// NesPpu reading is through a buffer and the results are off by one.
			value = ppuDataBuffer;
			ppuDataBuffer = ReadVRAM(vramAddr);
			renderer->ppuIncrementVramAddr(m_ppuCtrl & NesPpuCTRL_INCREMENT ? 32 : 1); // increment v
		}
		else {
			// I probably shouldn't support reading palette data
			// as some NES's themselves don't support it. No game should be doing this.
			// Reading palette data is grabbed right away
			// without the buffer, and the buffer is filled with the underlying nametable byte
			
			// Reading from palette RAM (mirrored every 32 bytes)
			uint8_t paletteAddr = vramAddr & 0x1F;
			if (paletteAddr >= 0x10 && (paletteAddr % 4 == 0)) {
				paletteAddr -= 0x10; // Mirror universal background color
			}
			value = paletteTable[paletteAddr];
			// But buffer is filled with the underlying nametable byte
			uint16_t mirroredAddr = 0x2000 + (vramAddr & 0xFFF); // Mirror nametables every 4KB
			ppuDataBuffer = bus->cart.mapper->readCHR(mirroredAddr);
			renderer->ppuIncrementVramAddr(m_ppuCtrl & NesPpuCTRL_INCREMENT ? 32 : 1); // increment v
		}
		if (m_mapper) {
			m_mapper->ClockIRQCounter(renderer->ppuGetVramAddr());
		}

		LOG(L"(%d) 0x%04X NesPpuDATA Read 0x%02X\n", bus->cpu.GetCycleCount(), bus->cpu.GetPC(), value);
		return value;
	}
	}

	return 0;
}

uint8_t NesPpu::peek_register(uint16_t addr)
{
	switch (addr)
	{
	case NesPpuCTRL: {
		return m_ppuCtrl;
	}
	case NesPpuMASK: {
		// not typically readable, return 0
		return 0;
	}
	case NesPpuSTATUS: {
		return m_ppuStatus;
	}
	case OAMADDR: {
		return oamAddr;
	}
	case OAMDATA: {
		return oam[oamAddr];
	}
	case NesPpuSCROLL: {
		return 0;
	}
	case NesPpuADDR: {
		return 0;
	}
	case NesPpuDATA: {
		return ppuDataBuffer;
	}
	}

	return 0;
}

uint8_t NesPpu::GetScrollX() const {
	return renderer->getScrollX();
}

uint8_t NesPpu::GetScrollY() const {
	return renderer->getScrollY();
}

uint16_t NesPpu::GetVRAMAddress() const {
	return renderer->getNesPpuAddr();
}

uint8_t NesPpu::PeekVRAM(uint16_t addr) {
	uint8_t value = 0;
	if (addr < 0x2000) {
		// Reading from CHR-ROM/RAM
		value = bus->cart.mapper->readCHR(addr);
	}
	else if (addr < 0x3F00) {
		// Reading from nametables and attribute tables
		addr = 0x2000 + (addr & 0x0FFF); // Mirror nametables every 4KB
		value = bus->cart.mapper->readCHR(addr);
	}
	else if (addr < 0x4000) {
		// Reading from palette RAM (mirrored every 32 bytes)
		uint8_t paletteAddr = addr & 0x1F;
		if (paletteAddr >= 0x10 && (paletteAddr % 4 == 0)) {
			paletteAddr -= 0x10; // Mirror universal background color
		}
		value = paletteTable[paletteAddr];
	}

	return value;
}

uint8_t NesPpu::ReadVRAM(uint16_t addr) {
	uint8_t value = 0;
	if (addr < 0x2000) {
		// Reading from CHR-ROM/RAM
		value = bus->cart.mapper->readCHR(addr);
		if (m_mapper) {
			m_mapper->ClockIRQCounter(addr);
		}
	}
	else if (addr < 0x3F00) {
		// Reading from nametables and attribute tables
		addr = 0x2000 + (addr & 0x0FFF); // Mirror nametables every 4KB
		value = bus->cart.mapper->readCHR(addr);
	}
	else if (addr < 0x4000) {
		// Reading from palette RAM (mirrored every 32 bytes)
		uint8_t paletteAddr = addr & 0x1F;
		if (paletteAddr >= 0x10 && (paletteAddr % 4 == 0)) {
			paletteAddr -= 0x10; // Mirror universal background color
		}
		value = paletteTable[paletteAddr];
	}

	return value;
}

void NesPpu::write_vram(uint16_t addr, uint8_t value)
{
	addr &= 0x3FFF; // Mask to 14 bits
	if (addr < 0x2000) {
		// Write to CHR-RAM (if enabled)
		bus->cart.mapper->writeCHR(addr, value);
		if (m_mapper) {
			m_mapper->ClockIRQCounter(addr);
		}
		return;
	}
	else if (addr < 0x3F00) {
		// Name tables and attribute tables
		addr = 0x2000 + (addr & 0x0FFF); // Mirror nametables every 4KB
		bus->cart.mapper->writeCHR(addr, value);
		return;
	}
	else if (addr < 0x4000) {
		// Palette RAM (mirrored every 32 bytes)
		// 3F00 = 0011 1111 0000 0000
		// 3F1F = 0011 1111 0001 1111
		uint8_t paletteAddr = addr & 0x1F; // 0001 1111
		if (paletteAddr % 4 == 0 && paletteAddr >= 0x10) {
			// Handle special mirroring of background color entries
			// These 4 addresses mirror to their lower counterparts
			paletteAddr -= 0x10;
		}
		// & 0x3F fixes Punch Out, which uses invalid entry 0x8f.
		paletteTable[paletteAddr] = value & 0x3F;
		//InvalidateRect(core->m_hwndPalette, NULL, FALSE); // Update palette window if open
		return;
	}
}

void NesPpu::UpdateState() {
	dbgContext->ppuState.ctrl = m_ppuCtrl;
	dbgContext->ppuState.mask = m_ppuMask;
	dbgContext->ppuState.status = m_ppuStatus;
	dbgContext->ppuState.scanline = renderer->m_scanline;
	dbgContext->ppuState.dot = renderer->dot;
	dbgContext->ppuState.bgPatternTableAddr = GetBackgroundPatternTableBase();
	dbgContext->ppuState.spritePatternTableAddr = GetSpritePatternTableBase(0); // Pass 0 just to get the base address for 8x8 sprites
	dbgContext->ppuState.scrollX = GetScrollX();
	dbgContext->ppuState.scrollY = GetScrollY();
	dbgContext->ppuState.mirrorMode = bus->cart.mapper->GetMirrorMode();
	memcpy(dbgContext->ppuState.palette.data(), paletteTable.data(), 32);
	memcpy(dbgContext->ppuState.oam.data(), oam.data(), 256);
	int nametableCount = bus->cart.mapper->GetMirrorMode() == MapperBase::MirrorMode::FOUR_SCREEN ? 4 : 2;
	memcpy(dbgContext->ppuState.nametables.data(), bus->cart.mapper->_vram.data(), 0x400 * nametableCount);
	// TODO - CHR memory read may be slow depending on mapper implementation
	// Consider memcpy by page?
	for (int i = 0; i < 0x2000; i++) {
		dbgContext->ppuState.chrMemory[i] = bus->cart.mapper->readCHR(i);
	}
}

void NesPpu::Clock() {
	// TODO Make the scanline and dot configurable since banks or scrolling may change during the frame render.
	if (renderer->m_scanline == 0 && renderer->dot == 0) {
		UpdateState();
	}
	renderer->clock(buffer);
}

uint8_t NesPpu::get_tile_pixel_color_index(uint8_t tileIndex, uint8_t pixelInTileX, uint8_t pixelInTileY, bool isSprite, bool isSecondSprite)
{
	if (isSprite) {
		int i = 0;
	}
	// Determine the pattern table base address
	uint16_t patternTableBase = isSprite ? GetSpritePatternTableBase(tileIndex) : GetBackgroundPatternTableBase();
	// This needs to be refactored, it's hard to understand.
	if (isSprite) {
		if (m_ppuCtrl & NesPpuCTRL_SPRITESIZE) { // 8x16
			if (tileIndex & 1) {
				if (!isSecondSprite) {
					tileIndex -= 1;
				}
			}
			else {
				if (isSecondSprite) {
					tileIndex += 1;
				}
			}
		}
	}
	
	int tileBase = patternTableBase + (tileIndex * 16); // 16 bytes per tile

	uint8_t byte1 = bus->cart.mapper->readCHR(tileBase + pixelInTileY);     // bitplane 0
	uint8_t byte2 = bus->cart.mapper->readCHR(tileBase + pixelInTileY + 8); // bitplane 1

	uint8_t bit0 = (byte1 >> (7 - pixelInTileX)) & 1;
	uint8_t bit1 = (byte2 >> (7 - pixelInTileX)) & 1;
	uint8_t colorIndex = (bit1 << 1) | bit0;

	return colorIndex;
}

void NesPpu::SetNesPpuStatus(uint8_t flag) {
	m_ppuStatus |= flag;
}

void NesPpu::get_palette(uint8_t paletteIndex, std::array<uint32_t, 4>& colors)
{
	// Each palette consists of 4 colors, starting from 0x3F00 in VRAM
	uint16_t paletteAddr = paletteIndex * 4;
	colors[0] = m_nesPalette[paletteTable[paletteAddr] & 0x3F];
	colors[1] = m_nesPalette[paletteTable[paletteAddr + 1] & 0x3F];
	colors[2] = m_nesPalette[paletteTable[paletteAddr + 2] & 0x3F];
	colors[3] = m_nesPalette[paletteTable[paletteAddr + 3] & 0x3F];
}

// The difference between FrameComplete and FrameTick is that FrameComplete marks we are in VBlank.
// FrameComplete cannot be modified outside of RendererLoopy - it is a rendering state.
// FrameTick is an event that informs the caller that a frame is complete. The caller will reset this event flag
// once handled. The caller, upon receiving this event, will do things like sending the audio buffer to the audio
// device, and other tasks in preparation for the next frame.
bool NesPpu::isFrameComplete() {
	return renderer->isFrameComplete();
}

bool NesPpu::isFrameTicked() {
	return renderer->m_frameTick;
}

void NesPpu::setFrameComplete(bool complete) {
	renderer->setFrameComplete(complete);
}

// ---------------- Debug helper ----------------
inline void NesPpu::dbg(const wchar_t* fmt, ...) {
#ifdef NesPpuDEBUG
	//if (!debug) return;
	wchar_t buf[512];
	va_list args;
	va_start(args, fmt);
	_vsnwprintf_s(buf, sizeof(buf) / sizeof(buf[0]), _TRUNCATE, fmt, args);
	va_end(args);
	OutputDebugStringW(buf);
#endif
}

void NesPpu::Serialize(Serializer& serializer) {
	renderer->Serialize(serializer);
	NesPpuState state = {};
	for (int i = 0; i < 0x100; i++) {
		state.oam[i] = oam[i];
	}
	state.oamAddr = oamAddr;
	for (int i = 0; i < 32; i++) {
		state.paletteTable[i] = paletteTable[i];
	}
	state.ppuMask = m_ppuMask;
	state.ppuStatus = m_ppuStatus;
	state.ppuCtrl = m_ppuCtrl;
	
	state.ppuDataBuffer = ppuDataBuffer;
	serializer.Write(state);
}

void NesPpu::Deserialize(Serializer& serializer) {
	renderer->Deserialize(serializer);
	NesPpuState state = {};
	serializer.Read(state);
	for (int i = 0; i < 0x100; i++) {
		oam[i] = state.oam[i];
	}
	oamAddr = state.oamAddr;
	for (int i = 0; i < 32; i++) {
		paletteTable[i] = state.paletteTable[i];
	}
	m_ppuMask = state.ppuMask;
	m_ppuStatus = state.ppuStatus;
	m_ppuCtrl = state.ppuCtrl;
	
	ppuDataBuffer = state.ppuDataBuffer;
}