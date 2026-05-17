#include "CNROM.h"

CNROM::CNROM() : MapperBase()
{
	MapperBase::SetPrgPageSize(0x8000);
	MapperBase::SetChrPageSize(0x2000);
}

void CNROM::RecomputePrgMappings() {
	MapperBase::SetPrgPage(0, 0);
}

void CNROM::RecomputeChrMappings() {
	MapperBase::SetChrPage(0, _chrBankReg);
}

void CNROM::writeRegister(uint16_t addr, uint8_t val, uint64_t currentCycle) {
	// Family Trainer: Jogging Race uses an oversized mapper.
	// Some unlicensed carts also use this mapper with more than 4 CHR banks.
	_chrBankReg = val & 0b1111;
	RecomputeMappings();
}

void CNROM::Serialize(Serializer& serializer) {
	MapperBase::Serialize(serializer);
	serializer.Write(_chrBankReg);
}

void CNROM::Deserialize(Serializer& serializer) {
	MapperBase::Deserialize(serializer);
	serializer.Read(_chrBankReg);
	RecomputeMappings();
}