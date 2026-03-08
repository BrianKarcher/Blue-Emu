#pragma once

#include <cstdint>
#include "MapperBase.h"

class Cartridge;

class AxROMMapper : public MapperBase
{
public:
	AxROMMapper(Cartridge* cartridge, uint8_t prgRom16kSize);
	void writeRegister(uint16_t addr, uint8_t val, uint64_t currentCycle);
	void RecomputePrgMappings() override;
	void RecomputeChrMappings() override;
	void shutdown() {}
	void Serialize(Serializer& serializer) override;
	void Deserialize(Serializer& serializer) override;

private:
	uint8_t* prgAddr = 0;
	uint8_t prgBank32kCount = 0;
	Cartridge* cartridge;
	bool nameTable;
	uint8_t prgBankSelect = 0;
};