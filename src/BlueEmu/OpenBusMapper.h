#pragma once
#include <cstdint>
#include "MemoryMapper.h"

class OpenBusMapper : public MemoryMapper
{
private:
	uint8_t openBus;

public:
	OpenBusMapper() : openBus(0) {

	}

	~OpenBusMapper() {

	}

	void setOpenBus(uint8_t value) {
		openBus = value;
	}

	inline uint8_t read(uint16_t address) {
		return openBus;
	}

	uint8_t peek(uint16_t address) {
		return openBus;
	}

	inline void write(uint16_t address, uint8_t value) {
		
	}
};