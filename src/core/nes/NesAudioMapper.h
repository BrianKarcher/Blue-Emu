#pragma once

#include <cstdint>
#include <array>
#include "NesMemoryMapper.h"

class NesApu;
class NesBus;

class NesAudioMapper : public NesMemoryMapper {
public:
	NesApu& apu;

	NesAudioMapper(NesApu& apu) : apu(apu) {

	}

	~NesAudioMapper() {

	}

	uint8_t read(uint16_t address);

	uint8_t peek(uint16_t address);

	void write(uint16_t address, uint8_t value);

	void register_memory(NesBus& bus);
};