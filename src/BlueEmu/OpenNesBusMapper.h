#pragma once
#include <cstdint>
#include "MemoryMapper.h"

class OpenNesBusMapper : public MemoryMapper
{
private:
	uint8_t openNesBus;

public:
	OpenNesBusMapper() : openNesBus(0) {

	}

	~OpenNesBusMapper() {

	}

	void setOpenNesBus(uint8_t value) {
		openNesBus = value;
	}

	inline uint8_t read(uint16_t address) {
		return openNesBus;
	}

	uint8_t peek(uint16_t address) {
		return openNesBus;
	}

	inline void write(uint16_t address, uint8_t value) {
		
	}
};