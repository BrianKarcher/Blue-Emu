#pragma once

#include <cstdint>
#include <array>
#include "MemoryMapper.h"

// 2KB internal RAM (mirrored)
class RAMMapper : public MemoryMapper {

public:
	std::array<uint8_t, 2048> cpuRAM{};

	RAMMapper() {

	}
	
	~RAMMapper() {

	}

	inline uint8_t read(uint16_t address) {
		return cpuRAM[address & 0x07FF];
	}

	uint8_t peek(uint16_t address) override {
		return cpuRAM[address & 0x07FF];
	}

	inline void write(uint16_t address, uint8_t value) {
		cpuRAM[address & 0x07FF] = value;
	}
};