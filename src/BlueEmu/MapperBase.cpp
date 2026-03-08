#include "MapperBase.h"
#include "Serializer.h"
#include <Windows.h>

void MapperBase::initialize(ines_file_t& data) {
	Mapper::initialize(data);
	for (int i = 0; i < 0x20; i++) {
		_isPpuPageWritable[i] = isCHRWritable;
	}
	for (int i = 0x20; i < 0x100; i++) {
		_isPpuPageWritable[i] = true;
	}
	_vram.resize(_nametableRamSize);
	m_mirrorMode = data.header.flags6 & 0x01 ? VERTICAL : HORIZONTAL;
	_prgRomSize = data.header.prg_rom_size * 16384;
	_chrRomSize = data.header.chr_rom_size * 8192;
	RecomputeMappings();
}

// ---------------- Debug helper ----------------
inline void MapperBase::dbg(const wchar_t* fmt, ...) const {
	wchar_t buf[512];
	va_list args;
	va_start(args, fmt);
	_vsnwprintf_s(buf, sizeof(buf) / sizeof(buf[0]), _TRUNCATE, fmt, args);
	va_end(args);
	OutputDebugStringW(buf);
}

void MapperBase::SetPrgPageSize(uint16_t pageSize) {
	_prgPageSize = pageSize;
	_prgPageCount = _prgRomSize / _prgPageSize;
}

void MapperBase::SetPrgPage(uint16_t pageIndex, uint8_t bank) {
	uint16_t startAddr = pageIndex * _prgPageSize;
	uint16_t endAddr = startAddr + _prgPageSize;
	// Calculate the offset into CHR ROM and mask it against total size
	// to prevent out-of-bounds memory access.
	uint32_t totalChrSize = (uint32_t)m_prgRomData.size();
	uint32_t bankOffset = ((uint32_t)bank * _prgPageSize) % totalChrSize;

	SetPrgRange(startAddr, endAddr, bankOffset);
}

void MapperBase::SetPrgRange(uint16_t startInclusive, uint16_t endExclusive, uint32_t bankOffset) {
	// We shift by 8 because the _chrPages array is indexed by 256-byte chunks
	// (0x2000 PPU range / 256 bytes = 32 entries)
	uint8_t startPage = startInclusive >> 8;
	uint8_t endPage = endExclusive >> 8;
	for (int i = startPage; i < endPage; i++) {
		// Calculate how many bytes into the requested bank we are
		uint32_t relativeOffset = (i - startPage) * 256;

		// Map the pointer to the specific 256-byte chunk in the ROM
		_prgPages[i] = &m_prgRomData[bankOffset + relativeOffset];
	}
}

void MapperBase::SetChrPageSize(uint16_t pageSize) {
	_chrPageSize = pageSize;
	_chrPageCount = _chrRomSize / _chrPageSize;
}

void MapperBase::SetChrPage(uint16_t pageIndex, uint8_t bank) {
	uint16_t startAddr = pageIndex * _chrPageSize;
	uint16_t endAddr = startAddr + _chrPageSize;
	// Calculate the offset into CHR ROM and mask it against total size
	// to prevent out-of-bounds memory access.
	uint32_t totalChrSize = (uint32_t)m_chrData.size();
	uint32_t bankOffset = ((uint32_t)bank * _chrPageSize) % totalChrSize;

	SetChrRange(startAddr, endAddr, m_chrData.data(), bankOffset);
}

void MapperBase::SetChrRange(uint16_t startInclusive, uint16_t endExclusive, uint8_t* source, uint32_t bankOffset) {
	// We shift by 8 because the _chrPages array is indexed by 256-byte chunks
	// (0x2000 PPU range / 256 bytes = 32 entries)
	uint8_t startPage = startInclusive >> 8;
	uint8_t endPage = endExclusive >> 8;
	LOG(L"Mapper SetChrRange: start=0x%04X end=0x%04X bank=0x%08X\n", startInclusive, endExclusive, bankOffset);
	for (int i = startPage; i < endPage; i++) {
		// Calculate how many bytes into the requested bank we are
		uint32_t relativeOffset = (i - startPage) * 256;

		// Map the pointer to the specific 256-byte chunk in the ROM
		_ppuPages[i] = &source[bankOffset + relativeOffset];
	}
}

void MapperBase::writePRGROM(uint16_t address, uint8_t data, uint64_t currentCycle) {
	writeRegister(address, data, currentCycle);
}

void MapperBase::writeCHR(uint16_t addr, uint8_t data) {
	uint8_t addrHi = addr >> 8;
	if (_isPpuPageWritable[addrHi]) {
		_ppuPages[addrHi][addr & 0xFF] = data;
	}
}

void MapperBase::RecomputeMappings() {
	RecomputePrgMappings();
	RecomputeChrMappings();
	RecomputeMirrorModeMapping();
}

void MapperBase::SetNametablePage(uint8_t virtualIndex, uint8_t physicalIndex) {
	uint16_t startAddr = 0x2000 + virtualIndex * _nametablePageSize;
	uint16_t endAddr = startAddr + _nametablePageSize;
	uint32_t bankOffset = ((uint32_t)physicalIndex * _nametablePageSize);

	SetChrRange(startAddr, endAddr, _vram.data(), bankOffset);
}

void MapperBase::RecomputeMirrorModeMapping() {
	// Map logical nametable to physical VRAM based on mirror mode
	switch (m_mirrorMode) {
	case HORIZONTAL:
		SetNametablePage(0, 0); // NT0 -> VRAM 0
		SetNametablePage(1, 0); // NT1 -> VRAM 0
		SetNametablePage(2, 1); // NT2 -> VRAM 1
		SetNametablePage(3, 1); // NT3 -> VRAM 1
		// $2000=$2400 (NT1), $2800=$2C00 (NT2)
		// Tables 0,1 map to first 1KB, tables 2,3 map to second 1KB
		//table = (table & 0x02) >> 1;
		break;

	case VERTICAL:
		SetNametablePage(0, 0); // NT0 -> VRAM 0
		SetNametablePage(1, 1); // NT1 -> VRAM 1
		SetNametablePage(2, 0); // NT2 -> VRAM 0
		SetNametablePage(3, 1); // NT3 -> VRAM 1
		break;

	case SINGLE_LOWER:
		// All nametables map to first 1KB
		SetNametablePage(0, 0); // NT0 -> VRAM 0
		SetNametablePage(1, 0); // NT1 -> VRAM 0
		SetNametablePage(2, 0); // NT2 -> VRAM 0
		SetNametablePage(3, 0); // NT3 -> VRAM 0
		break;

	case SINGLE_UPPER:
		// All nametables map to second 1KB
		SetNametablePage(0, 1); // NT0 -> VRAM 1
		SetNametablePage(1, 1); // NT1 -> VRAM 1
		SetNametablePage(2, 1); // NT2 -> VRAM 1
		SetNametablePage(3, 1); // NT3 -> VRAM 1		
		break;

	case FOUR_SCREEN:
		// Each nametable maps to its own 1KB (no mirroring)
		// This needs external RAM on the cartridge
		SetNametablePage(0, 0); // NT0 -> VRAM 0
		SetNametablePage(1, 1); // NT1 -> VRAM 1
		SetNametablePage(2, 2); // NT2 -> VRAM 2
		SetNametablePage(3, 3); // NT3 -> VRAM 3		
		break;
	}
}

MapperBase::MirrorMode MapperBase::GetMirrorMode() {
	return m_mirrorMode;
}

void MapperBase::SetMirrorMode(MirrorMode mirrorMode) {
	m_mirrorMode = mirrorMode;
	RecomputeMirrorModeMapping();
}

void MapperBase::shutdown() {

}

void MapperBase::Serialize(Serializer& serializer) {
	Mapper::Serialize(serializer);
	serializer.WriteVector(_vram);
}

void MapperBase::Deserialize(Serializer& serializer) {
	Mapper::Deserialize(serializer);
	serializer.ReadVector(_vram);
}