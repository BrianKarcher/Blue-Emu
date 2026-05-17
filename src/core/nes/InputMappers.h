#pragma once
#include "NesMemoryMapper.h"

class NesInput;
class NesBus;

class ReadController1Mapper : public NesMemoryMapper
{
public:
	ReadController1Mapper(NesInput& input);
	~ReadController1Mapper() = default;

	inline uint8_t read(uint16_t address);
	uint8_t peek(uint16_t address) {
		return 0;
	}
	inline void write(uint16_t address, uint8_t value);
	void register_memory(NesBus& bus);
private:
	NesInput& m_input;
};

class ReadController2Mapper : public NesMemoryMapper
{
public:
	ReadController2Mapper(NesInput& input);
	~ReadController2Mapper() = default;

	inline uint8_t read(uint16_t address);
	uint8_t peek(uint16_t address) {
		return 0;
	}
	inline void write(uint16_t address, uint8_t value);
	void register_memory(NesBus& bus);
private:
	NesInput& m_input;
};