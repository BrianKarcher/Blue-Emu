#include "MapperBase.h"
#include "NesBus.h"
#include "Serializer.h"
#include <Windows.h>

void MapperBase::initialize(uint8_t *prg_rom_data, size_t prg_rom_size, uint8_t *chr_rom_data, size_t chr_rom_size, MirrorMode mirror_mode) {
	m_prgRomDataSize = prg_rom_size;

	if (chr_rom_size == 0) {
		isCHRWritable = true;
		// No CHR ROM present; allocate 8KB of CHR RAM
		m_chrDataSize = 0x2000;
	}
	else {
		isCHRWritable = false;
		m_chrDataSize = chr_rom_size;
	}

	SetPRGRom(prg_rom_data, m_prgRomDataSize);
	SetCHRRom(chr_rom_data, m_chrDataSize);

	for (int i = 0; i < 0x20; i++) {
		_isPpuPageWritable[i] = isCHRWritable;
	}
	for (int i = 0x20; i < 0x100; i++) {
		_isPpuPageWritable[i] = true;
	}
	
	m_mirrorMode = mirror_mode;
	//_prgRomSize = data.header.prg_rom_size * 16384;
	//_chrRomSize = data.header.chr_rom_size * 8192;
	initialize_vram();
	RecomputeMappings();
}

// An alternate is to have the caller manage the memory and just pass in pointers
// But this way we can ensure the memory is always owned by the mapper and not accidentally freed by the caller.
void MapperBase::SetCHRRom(uint8_t* data, size_t size) {
	if (!m_chrData)
	{
		m_chrData = (uint8_t*)malloc(size);
	}
	else
	{
		m_chrData = (uint8_t*)realloc(m_chrData, size);
	}
	if (data)
	{
		memcpy(m_chrData, data, size);
	}
}

void MapperBase::SetPRGRom(uint8_t* data, size_t size) {
	if (!m_prgRomData)
	{
		m_prgRomData = (uint8_t*)malloc(size * sizeof(uint8_t));
	}
	else
	{
		m_prgRomData = (uint8_t*)realloc(m_prgRomData, size * sizeof(uint8_t));
	}

	memcpy(m_prgRomData, data, size);
}

void MapperBase::initialize_vram() {
	_vram.resize(_nametableRamSize);
}

uint8_t MapperBase::read(uint16_t address) {
	if (address < 0x8000) {
		// TODO : Improve the performance of this
		return m_prgRamData[address - 0x6000];
	}
	else {
		return readPRGROM(address); //m_prgRomData[address];
	}
}

uint8_t MapperBase::peek(uint16_t address) {
	if (address < 0x8000) {
		// TODO : Improve the performance of this
		return m_prgRamData[address - 0x6000];
	}
	else {
		return readPRGROM(address); //m_prgRomData[address];
	}
}

void MapperBase::write(uint16_t address, uint8_t value) {
	if (address < 0x8000) {
		m_prgRamData[address - 0x6000] = value;
	}
	else {
		writeRegister(address, value, 0);
	}
}

void MapperBase::register_memory(NesBus& bus) {
	bus.ReadRegisterAdd(0x6000, 0xFFFF, this);
	bus.WriteRegisterAdd(0x6000, 0xFFFF, this);
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
	_prgPageCount = m_prgRomDataSize / _prgPageSize;
}

void MapperBase::SetPrgPage(uint16_t pageIndex, uint8_t bank) {
	uint16_t startAddr = pageIndex * _prgPageSize;
	uint16_t endAddr = startAddr + _prgPageSize;
	// Calculate the offset into CHR ROM and mask it against total size
	// to prevent out-of-bounds memory access.
	uint32_t totalChrSize = (uint32_t)m_prgRomDataSize;
	uint32_t bankOffset = ((uint32_t)bank * _prgPageSize) % totalChrSize;

	SetPrgRange(startAddr, endAddr, bankOffset);
}

void MapperBase::SetPrgRange(uint16_t startInclusive, uint16_t endExclusive, uint32_t bankOffset) {
	// We shift by 8 because the _prgPages array is indexed by 256-byte chunks
	// (0x2000 NesPpu range / 256 bytes = 32 entries)
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
	_chrPageCount = m_chrDataSize / _chrPageSize;
}

void MapperBase::SetChrPage(uint16_t pageIndex, uint8_t bank) {
	uint16_t startAddr = pageIndex * _chrPageSize;
	uint16_t endAddr = startAddr + _chrPageSize;
	// Calculate the offset into CHR ROM and mask it against total size
	// to prevent out-of-bounds memory access.
	uint32_t totalChrSize = (uint32_t)m_chrDataSize;
	uint32_t bankOffset = ((uint32_t)bank * _chrPageSize) % totalChrSize;

	SetChrRange(startAddr, endAddr, m_chrData, bankOffset);
}

void MapperBase::SetChrRange(uint16_t startInclusive, uint16_t endExclusive, uint8_t* source, uint32_t bankOffset) {
	// We shift by 8 because the _chrPages array is indexed by 256-byte chunks
	// (0x2000 NesPpu range / 256 bytes = 32 entries)
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
	free(m_prgRomData);
	m_prgRomData = NULL;
	free(m_chrData);
	m_chrData = NULL;
	m_prgRamData.clear();
}

void MapperBase::Serialize(Serializer& serializer) {
	serializer.WriteVector(m_prgRamData);
	if (isCHRWritable) {
		for (int i = 0; i < m_chrDataSize; ++i)
		{
			serializer.Write(m_chrData[i]);
		}
	}
	serializer.WriteVector(_vram);
}

void MapperBase::Deserialize(Serializer& serializer) {
	serializer.ReadVector(m_prgRamData);
	if (isCHRWritable) {
		for (int i = 0; i < m_chrDataSize; ++i)
		{
			serializer.Read(m_chrData[i]);
		}
	}
	serializer.ReadVector(_vram);
}