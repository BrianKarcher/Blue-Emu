#include "NROM.h"
#include "Cartridge.h"

NROM::NROM(Cartridge* cartridge) {
	MapperBase::SetPrgPageSize(0x4000);
	MapperBase::SetChrPageSize(0x2000);
	this->cartridge = cartridge;
}

void NROM::writeRegister(uint16_t addr, uint8_t val, uint64_t currentCycle) {

}

void NROM::RecomputePrgMappings() {
	if (m_prgRomData.size() < 0x8000) {
		// 32 KB PRG ROM mirrored
		MapperBase::SetPrgPage(0, 0);
		MapperBase::SetPrgPage(1, 0);
	}
	else {
		MapperBase::SetPrgPage(0, 0);
		MapperBase::SetPrgPage(1, 1);
	}
}
void NROM::RecomputeChrMappings() {
	MapperBase::SetChrPage(0, 0);
}