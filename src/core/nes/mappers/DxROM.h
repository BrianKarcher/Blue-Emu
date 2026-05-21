#pragma once
#include "MapperBase.h"
#include <cstdint>

class DxROM : public MapperBase {
public:
	DxROM(bool alternative_nametable_layout) : alternative_nametable_layout(alternative_nametable_layout), MapperBase() {
		
	}
	void writeRegister(uint16_t addr, uint8_t val, uint64_t currentCycle) override;
	void initialize(uint8_t* prg_rom_data, size_t prg_rom_size, uint8_t* chr_rom_data, size_t chr_rom_size, MirrorMode mirror_mode) override;
	void RecomputePrgMappings() override;
	void RecomputeChrMappings() override;
	void Serialize(Serializer& serializer) override;
	void Deserialize(Serializer& serializer) override;

private:
	uint8_t _banks[8] = { 0 };
	uint8_t _regSelect;
	bool alternative_nametable_layout;
};