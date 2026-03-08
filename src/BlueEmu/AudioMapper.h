#pragma once

#include <cstdint>
#include <array>
#include "MemoryMapper.h"

class APU;
class Bus;

class AudioMapper : public MemoryMapper {
public:
	APU& apu;

	AudioMapper(APU& apu) : apu(apu) {

	}

	~AudioMapper() {

	}

	uint8_t read(uint16_t address);

	uint8_t peek(uint16_t address);

	void write(uint16_t address, uint8_t value);

	void register_memory(Bus& bus);
};