#pragma once
#include <cstdint>
#include <vector>
#include "MemoryMapper.h"
#include "INESLoader.h"
#include "Serializer.h"

class Cartridge;
class Bus;

class Mapper : public MemoryMapper {
public:
	std::vector<uint8_t> m_prgRomData;
	std::vector<uint8_t> m_prgRamData;
	std::vector<uint8_t> m_chrData;
	bool isCHRWritable;

	virtual void initialize(ines_file_t& data);
	virtual void writeRegister(uint16_t addr, uint8_t val, uint64_t currentCycle) = 0;
	virtual inline uint8_t readPRGROM(uint16_t addr) const = 0;
	virtual void writePRGROM(uint16_t address, uint8_t data, uint64_t currentCycle) = 0;
	virtual inline uint8_t readCHR(uint16_t addr) = 0;
	virtual void writeCHR(uint16_t addr, uint8_t data) = 0;
	virtual void shutdown() = 0;
	virtual bool IrqPending() { return false; }

	uint8_t read(uint16_t address);
	uint8_t peek(uint16_t address);
	void write(uint16_t address, uint8_t value);
	void register_memory(Bus& bus);

	virtual void Serialize(Serializer& serializer) = 0;
	virtual void Deserialize(Serializer& serializer) = 0;

	void SetCHRRom(uint8_t* data, size_t size);
	void SetPRGRom(uint8_t* data, size_t size);
private:

};