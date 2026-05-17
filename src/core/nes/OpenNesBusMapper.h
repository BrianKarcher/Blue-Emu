#pragma once
#include <cstdint>
#include "NesMemoryMapper.h"

class OpenNesBusMapper : public NesMemoryMapper
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