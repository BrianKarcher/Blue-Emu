#pragma once
#include <cstdint>
#include "MapperBase.h"

class Bus;
class Cartridge;

class MMC2Mapper : public MapperBase
{
public:
    MMC2Mapper(Bus& bus, uint8_t prgRomSize, uint8_t chrRomSize);

    void writeRegister(uint16_t addr, uint8_t val, uint64_t currentCycle) override;
    void RecomputePrgMappings() override;
    void RecomputeChrMappings() override;

    // Crucial for MMC2: We must intercept PPU reads to handle the bank latches
    inline uint8_t readCHR(uint16_t addr) override;

    void Serialize(Serializer& serializer) override;
    void Deserialize(Serializer& serializer) override;

private:
    inline void dbg(const wchar_t* fmt, ...);

    uint8_t prg_bank_select;

    // Latches for the two CHR banks
    // 0 = $FD8 latch, 1 = $FE8 latch
    uint8_t chr_bank_0[2];
    uint8_t chr_bank_1[2];

    uint8_t latch_0; // Current selection for bank 0 (0 or 1)
    uint8_t latch_1; // Current selection for bank 1 (0 or 1)

    uint8_t prgBank8kCount;
    Bus& bus;
    Cartridge& cart;
};