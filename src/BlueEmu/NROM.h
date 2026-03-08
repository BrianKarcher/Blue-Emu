#pragma once
#include <cstdint>
#include "MapperBase.h"

class Cartridge;

class NROM : public MapperBase {
public:
	NROM(Cartridge* cartridge);
	void writeRegister(uint16_t addr, uint8_t val, uint64_t currentCycle);
	void shutdown() { }
	void Serialize(Serializer& serializer) override {
		MapperBase::Serialize(serializer);
	}
	void Deserialize(Serializer& serializer) override {
		MapperBase::Deserialize(serializer);
	}
	void RecomputePrgMappings() override;
	void RecomputeChrMappings() override;

private:
	Cartridge* cartridge;
};