#include "UxROMMapper.h"
#include "MMC3.h"
#include "CPU.h"
#include "Cartridge.h"
#include "Bus.h"
#include "PPU.h"
#include "RendererLoopy.h"
#include <array>

#define BANK_SIZE_PRG 0x4000 // 16KB

// ---------------- Debug helper ----------------
inline void UxROMMapper::dbg(const wchar_t* fmt, ...) {
#ifdef MMC3DEBUG
	//if (!debug) return;
	wchar_t buf[512];
	va_list args;
	va_start(args, fmt);
	_vsnwprintf_s(buf, sizeof(buf) / sizeof(buf[0]), _TRUNCATE, fmt, args);
	va_end(args);
	OutputDebugStringW(buf);
#endif
}

UxROMMapper::UxROMMapper(Bus& b, uint8_t prgRomSize, uint8_t chrRomSize) : bus(b), cpu(b.cpu) {
	MapperBase::SetPrgPageSize(0x4000);
	MapperBase::SetChrPageSize(0x2000);
	prgBank16kCount = prgRomSize;
	this->cart = &bus.cart;
	// Start with bank 0 selected
	prg_bank_select = 0;
}

void UxROMMapper::writeRegister(uint16_t addr, uint8_t val, uint64_t currentCycle) {
	LOG(L"(%d) 0x%04X 0x%02X (%d) \n", currentCycle, addr, val, val);
	// This boards uses only the lower 3 bits to select the bank
	// The bank to load is stored in ROM. The address just points to the ROM location to pull.
	//uint8_t data = cart->m_prgRomData[addr];
	//prgMap[0] = &cart->m_prgRomData[(val & (prgBank16kCount - 1)) * BANK_SIZE_PRG];
	// Any write to $8000-$FFFF sets the bank select register
	// Only lower bits are used (depends on number of banks)
	// For example, 8 banks uses 3 bits, 16 banks uses 4 bits

	uint8_t bank_mask = 0;
	if (prgBank16kCount <= 2) bank_mask = 0x01;
	else if (prgBank16kCount <= 4) bank_mask = 0x03;
	else if (prgBank16kCount <= 8) bank_mask = 0x07;
	else if (prgBank16kCount <= 16) bank_mask = 0x0F;
	else bank_mask = 0x1F;

	prg_bank_select = val & bank_mask;
	RecomputeMappings();
}

void UxROMMapper::RecomputePrgMappings() {
	MapperBase::SetPrgPage(0, prg_bank_select);
	MapperBase::SetPrgPage(1, prgBank16kCount - 1);
}

void UxROMMapper::RecomputeChrMappings() {
	MapperBase::SetChrPage(0, 0); // UxROM typically has CHR RAM, so just map first page
}

void UxROMMapper::Serialize(Serializer& serializer) {
	MapperBase::Serialize(serializer);
	serializer.Write(prg_bank_select);
}

void UxROMMapper::Deserialize(Serializer& serializer) {
	MapperBase::Deserialize(serializer);
	serializer.Read(prg_bank_select);
}