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