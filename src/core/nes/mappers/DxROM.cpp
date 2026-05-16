#include "DxROM.h"
#include "MapperBase.h"

void DxROM::initialize(ines_file_t& data) {
	if (data.header.flags6 & FLAG_6_NAMETABLE_LAYOUT) {
		_nametableRamSize = 0x1000; // 4 screen
	}
	MapperBase::initialize(data);
	MapperBase::SetPrgPageSize(0x2000);
	MapperBase::SetChrPageSize(0x400);
	if (data.header.flags6 & FLAG_6_NAMETABLE_LAYOUT) {
		MapperBase::SetMirrorMode(MapperBase::MirrorMode::FOUR_SCREEN);
	}
}

void DxROM::RecomputeChrMappings() {
	MapperBase::SetChrPage(0, _banks[0]);
	MapperBase::SetChrPage(1, _banks[0] + 1);
	MapperBase::SetChrPage(2, _banks[1]);
	MapperBase::SetChrPage(3, _banks[1] + 1);
	MapperBase::SetChrPage(4, _banks[2]);
	MapperBase::SetChrPage(5, _banks[3]);
	MapperBase::SetChrPage(6, _banks[4]);
	MapperBase::SetChrPage(7, _banks[5]);
}

void DxROM::RecomputePrgMappings() {
	MapperBase::SetPrgPage(0, _banks[6]); // 8000
	MapperBase::SetPrgPage(1, _banks[7]); // A000
	MapperBase::SetPrgPage(2, _prgPageCount - 2); // C000
	MapperBase::SetPrgPage(3, _prgPageCount - 1); // E000
}

void DxROM::writeRegister(uint16_t addr, uint8_t val, uint64_t currentCycle) {
	//LOG(L"(%d) 0x%04X 0x%02X (%d) \n", currentCycle, addr, val, val);
	switch (addr & 0xE001) {
	case 0x8000:
		_regSelect = val & 0x7; // 3 LSB
		//LOG(L"prg: %d chr: %d reg: %d\n", prgMode, chrMode, m_regSelect);
		RecomputeMappings();
		break;

	case 0x8001:
		_banks[_regSelect] = val;

		if (_regSelect <= 1) {
			_banks[_regSelect] &= ~1;    // R0/R1: ignore bit 0
		}
		if (_regSelect >= 6) {
			_banks[_regSelect] &= 0xF;   // R6/R7: 4-bit
		}

		if (_regSelect < 6) {
			_banks[_regSelect] &= 0x3F; // CHR banks: 6-bit
			RecomputeChrMappings();
		}
		else {
			RecomputePrgMappings();
		}
		break;
	}
}

void DxROM::Serialize(Serializer& serializer) {
	MapperBase::Serialize(serializer);
	serializer.Write(_banks, 8);
	serializer.Write(_regSelect);
}

void DxROM::Deserialize(Serializer& serializer) {
	MapperBase::Deserialize(serializer);
	serializer.Read(_banks, 8);
	serializer.Read(_regSelect);
	RecomputeMappings();
}