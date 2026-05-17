#include "InputMappers.h"
#include "NesInput.h"
#include "NesBus.h"

ReadController1Mapper::ReadController1Mapper(NesInput& input) : m_input(input) { }

inline uint8_t ReadController1Mapper::read(uint16_t address) {
	return m_input.ReadController1();
}

uint8_t peek(uint16_t address) {
	return 0;
}

inline void ReadController1Mapper::write(uint16_t address, uint8_t value) {
	m_input.Poll();
}

void ReadController1Mapper::register_memory(NesBus& bus) {
	bus.ReadRegisterAdd(0x4016, 0x4016, this);
	bus.WriteRegisterAdd(0x4016, 0x4016, this);
}

ReadController2Mapper::ReadController2Mapper(NesInput& input) : m_input(input) {}

inline uint8_t ReadController2Mapper::read(uint16_t address) {
	return m_input.ReadController2();
}

inline void ReadController2Mapper::write(uint16_t address, uint8_t value) {
	// open bus
}

void ReadController2Mapper::register_memory(NesBus& bus) {
	bus.ReadRegisterAdd(0x4017, 0x4017, this);
}