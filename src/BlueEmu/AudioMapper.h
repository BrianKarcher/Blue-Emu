#pragma once

#include <cstdint>
#include <array>
#include "MemoryMapper.h"

class NesApu;
class NesBus;

class AudioMapper : public MemoryMapper {
public:
	NesApu& apu;

	AudioMapper(NesApu& apu) : apu(apu) {

	}

	~AudioMapper() {

	}

	uint8_t read(uint16_t address);

	uint8_t peek(uint16_t address);

	void write(uint16_t address, uint8_t value);

	void register_memory(NesBus& bus);
};