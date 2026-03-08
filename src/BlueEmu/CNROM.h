#pragma once
#include "MapperBase.h"

class CNROM : public MapperBase
{
public:
	CNROM();
	void RecomputePrgMappings() override;
	void RecomputeChrMappings() override;
	void writeRegister(uint16_t addr, uint8_t val, uint64_t currentCycle);

	void Serialize(Serializer& serializer) override;
	void Deserialize(Serializer& serializer) override;
	uint8_t _chrBankReg = 0;
};