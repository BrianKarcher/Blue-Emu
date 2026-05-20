#include "Mapper.h"
#include "NesBus.h"

uint8_t Mapper::read(uint16_t address) {
	if (address < 0x8000) {
		// TODO : Improve the performance of this
		return m_prgRamData[address - 0x6000];
	}
	else {
		return readPRGROM(address); //m_prgRomData[address];
	}
}

uint8_t Mapper::peek(uint16_t address) {
	if (address < 0x8000) {
		// TODO : Improve the performance of this
		return m_prgRamData[address - 0x6000];
	}
	else {
		return readPRGROM(address); //m_prgRomData[address];
	}
}

void Mapper::write(uint16_t address, uint8_t value) {
	if (address < 0x8000) {
		m_prgRamData[address - 0x6000] = value;
	}
	else {
		writeRegister(address, value, 0);
	}
}

void Mapper::register_memory(NesBus& bus) {
	bus.ReadRegisterAdd(0x6000, 0xFFFF, this);
	bus.WriteRegisterAdd(0x6000, 0xFFFF, this);
}

void Mapper::initialize(ines_file_t& inesFile) {
	m_prgRomDataSize = inesFile.prg_rom->size;

	if (inesFile.chr_rom->size == 0) {
		isCHRWritable = true;
		// No CHR ROM present; allocate 8KB of CHR RAM
		m_chrDataSize = 0x2000;
	}
	else {
		isCHRWritable = false;
		m_chrDataSize = inesFile.chr_rom->size;
	}
	
	SetPRGRom(inesFile.prg_rom->data, m_prgRomDataSize);
	SetCHRRom(inesFile.chr_rom->data, m_chrDataSize);
}

// An alternate is to have the caller manage the memory and just pass in pointers
// But this way we can ensure the memory is always owned by the mapper and not accidentally freed by the caller.
void Mapper::SetCHRRom(uint8_t* data, size_t size) {
	if (!m_chrData)
	{
		m_chrData = (uint8_t*)malloc(m_chrDataSize * sizeof(uint8_t));
	}
	else
	{
		m_chrData = (uint8_t*)realloc(m_chrData, m_chrDataSize * sizeof(uint8_t));
	}
	memcpy(m_chrData, data, size);
}

void Mapper::SetPRGRom(uint8_t* data, size_t size) {
	// Pad PRG data to at least 32KB
	// We need to make sure the vectors exist (IRQ vectors at $FFFA-$FFFF)
	// Even if they're zeroes.
	if (size < 0x8000) {
		size = 0x8000;
	}
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

void Mapper::Serialize(Serializer& serializer) {
	serializer.WriteVector(m_prgRamData);
	if (isCHRWritable) {
		for (int i = 0; i < m_chrDataSize; ++i)
		{
			serializer.Write(m_chrData[i]);
		}
	}
}

void Mapper::Deserialize(Serializer& serializer) {
	serializer.ReadVector(m_prgRamData);
	if (isCHRWritable) {
		for (int i = 0; i < m_chrDataSize; ++i)
		{
			serializer.Read(m_chrData[i]);
		}
	}
}