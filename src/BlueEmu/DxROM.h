#pragma once
#include "MapperBase.h"
#include <cstdint>

class DxROM : public MapperBase {
public:
	void writeRegister(uint16_t addr, uint8_t val, uint64_t currentCycle) override;
	void initialize(ines_file_t& data) override;
	void RecomputePrgMappings() override;
	void RecomputeChrMappings() override;
	void Serialize(Serializer& serializer) override;
	void Deserialize(Serializer& serializer) override;

private:
	uint8_t _banks[8] = { 0 };
	uint8_t _regSelect;
};