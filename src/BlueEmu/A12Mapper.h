#pragma once
#include <cstdint>

class A12Mapper {
public:
	virtual void ClockIRQCounter(uint16_t ppu_address) = 0;
};