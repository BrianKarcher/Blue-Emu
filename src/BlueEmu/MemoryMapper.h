#pragma once
#include <cstdint>

class MemoryMapper
{
public:
	virtual ~MemoryMapper() = default;
	virtual uint8_t read(uint16_t address) = 0;
	virtual uint8_t peek(uint16_t address) = 0;
	virtual void write(uint16_t address, uint8_t value) = 0;
};