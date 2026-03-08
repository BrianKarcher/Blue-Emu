#include "AudioMapper.h"
#include "Bus.h"
#include "APU.h"

void AudioMapper::register_memory(Bus& bus) {
	bus.WriteRegisterAdd(0x4000, 0x4013, this);
	bus.WriteRegisterAdd(0x4015, 0x4015, this);
	bus.WriteRegisterAdd(0x4017, 0x4017, this);

	bus.ReadRegisterAdd(0x4015, 0x4015, this);
}

uint8_t AudioMapper::read(uint16_t address) {
	return apu.read_register(address);
}

uint8_t AudioMapper::peek(uint16_t address) {
	return apu.peek_register(address);
}

void AudioMapper::write(uint16_t address, uint8_t value) {
	apu.write_register(address, value);
}