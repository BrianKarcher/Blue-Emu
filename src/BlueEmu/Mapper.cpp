#include "Mapper.h"
#include "Bus.h"

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

void Mapper::register_memory(Bus& bus) {
	bus.ReadRegisterAdd(0x6000, 0xFFFF, this);
	bus.WriteRegisterAdd(0x6000, 0xFFFF, this);
}

void Mapper::initialize(ines_file_t& inesFile) {
	m_prgRomData.clear();
	for (int i = 0; i < inesFile.prg_rom->size; i++) {
		m_prgRomData.push_back(inesFile.prg_rom->data[i]);
	}
	m_chrData.clear();
	if (inesFile.chr_rom->size == 0) {
		isCHRWritable = true;
		// No CHR ROM present; allocate 8KB of CHR RAM
		m_chrData.resize(0x2000, 0);
	}
	else {
		isCHRWritable = false;
		for (int i = 0; i < inesFile.chr_rom->size; i++) {
			m_chrData.push_back(inesFile.chr_rom->data[i]);
		}
	}
}

// For testing purposes
void Mapper::SetCHRRom(uint8_t* data, size_t size) {
	m_chrData.resize(size);
	memcpy(m_chrData.data(), data, size);
}

// For testing purposes
void Mapper::SetPRGRom(uint8_t* data, size_t size) {
	if (m_prgRomData.size() < size) {
		m_prgRomData.resize(size);
	}
	// Pad PRG data to at least 32KB
	// We need to make sure the vectors exist (IRQ vectors at $FFFA-$FFFF)
	// Even if they're zeroes.
	if (m_prgRomData.size() < 0x8000) {
		m_prgRomData.resize(0x8000);
	}
	memcpy(m_prgRomData.data(), data, size);
}

void Mapper::Serialize(Serializer& serializer) {
	serializer.WriteVector(m_prgRamData);
	if (isCHRWritable) {
		serializer.WriteVector(m_chrData);
	}
}

void Mapper::Deserialize(Serializer& serializer) {
	serializer.ReadVector(m_prgRamData);
	if (isCHRWritable) {
		serializer.ReadVector(m_chrData);
	}
}