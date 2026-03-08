#include "AxROMMapper.h"
#include "Cartridge.h"
#include "PPU.h"

AxROMMapper::AxROMMapper(Cartridge* cartridge, uint8_t prgRom16kSize) : cartridge(cartridge) {
	MapperBase::SetPrgPageSize(0x8000);
	MapperBase::SetChrPageSize(0x2000);
	prgBank32kCount = prgRom16kSize / 2;
	//prgBankSelect = prgBank32kCount - 1; // Default to last bank
	prgBankSelect = 0;
}

void AxROMMapper::writeRegister(uint16_t addr, uint8_t val, uint64_t currentCycle) {
	prgBankSelect = val & 0x07; // Select 32KB PRG bank (3 bits)
	prgBankSelect &= (prgBank32kCount - 1);
	nameTable = val & 0x10;
	SetMirrorMode(nameTable ? MapperBase::MirrorMode::SINGLE_UPPER : MapperBase::MirrorMode::SINGLE_LOWER);
	RecomputeMappings();
}

void AxROMMapper::RecomputePrgMappings() {
	MapperBase::SetPrgPage(0, prgBankSelect);
}

void AxROMMapper::RecomputeChrMappings() {
	MapperBase::SetChrPage(0, 0); // No CHR banking, always first bank
}

void AxROMMapper::Serialize(Serializer& serializer) {
	MapperBase::Serialize(serializer);
	serializer.Write(nameTable);
	serializer.Write(prgBankSelect);
}

void AxROMMapper::Deserialize(Serializer& serializer) {
	MapperBase::Deserialize(serializer);
	serializer.Read(nameTable);
	serializer.Read(prgBankSelect);
	RecomputeMappings();
}