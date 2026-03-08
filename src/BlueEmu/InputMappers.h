#pragma once
#include "MemoryMapper.h"

class Input;
class Bus;

class ReadController1Mapper : public MemoryMapper
{
public:
	ReadController1Mapper(Input& input);
	~ReadController1Mapper() = default;

	inline uint8_t read(uint16_t address);
	uint8_t peek(uint16_t address) {
		return 0;
	}
	inline void write(uint16_t address, uint8_t value);
	void register_memory(Bus& bus);
private:
	Input& m_input;
};

class ReadController2Mapper : public MemoryMapper
{
public:
	ReadController2Mapper(Input& input);
	~ReadController2Mapper() = default;

	inline uint8_t read(uint16_t address);
	uint8_t peek(uint16_t address) {
		return 0;
	}
	inline void write(uint16_t address, uint8_t value);
	void register_memory(Bus& bus);
private:
	Input& m_input;
};