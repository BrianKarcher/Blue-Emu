#include "MMC2Mapper.h"
#include "Bus.h"
#include "Cartridge.h"

MMC2Mapper::MMC2Mapper(Bus& b, uint8_t prgRomSize, uint8_t chrRomSize) : bus(b), cart(bus.cart) {
    MapperBase::SetPrgPageSize(0x2000); // MMC2 uses 8KB PRG pages
    MapperBase::SetChrPageSize(0x1000); // MMC2 uses 4KB CHR pages

    prgBank8kCount = prgRomSize * 2; // iNES gives size in 16KB, we use 8KB

    prg_bank_select = 0;
    chr_bank_0[0] = chr_bank_0[1] = 0;
    chr_bank_1[0] = chr_bank_1[1] = 0;
    //latch_0 = 0xFE; // Initial state usually FE
    //latch_1 = 0xFE;
}

inline uint8_t MMC2Mapper::readCHR(uint16_t addr) {
    uint8_t data = MapperBase::readCHR(addr);

    // MMC2 Latch Logic
    // Latch 0 ($0000-$0FFF)
    if (addr == 0x0FD8) {
        latch_0 = 0;
        RecomputeMappings();
    }
    else if (addr == 0x0FE8) {
        latch_0 = 1;
        RecomputeMappings();
    }
    // Latch 1 ($1000-$1FFF)
    else if (addr >= 0x1FD8 && addr <= 0x1FDF) {
        latch_1 = 0;
        RecomputeMappings();
    }
    else if (addr >= 0x1FE8 && addr <= 0x1FEF) {
        latch_1 = 1;
        RecomputeMappings();
    }

    return data;
}

void MMC2Mapper::writeRegister(uint16_t addr, uint8_t val, uint64_t currentCycle) {
    switch (addr & 0xF000) {
    case 0xA000: // PRG Select
        prg_bank_select = val & 0x0F;
        break;
    case 0xB000: // CHR Bank 0 ($0000) - FD latch
        chr_bank_0[0] = val & 0x1F;
        break;
    case 0xC000: // CHR Bank 0 ($0000) - FE latch
        chr_bank_0[1] = val & 0x1F;
        break;
    case 0xD000: // CHR Bank 1 ($1000) - FD latch
        chr_bank_1[0] = val & 0x1F;
        break;
    case 0xE000: // CHR Bank 1 ($1000) - FE latch
        chr_bank_1[1] = val & 0x1F;
        break;
    case 0xF000: // Mirroring
        SetMirrorMode((val & 0x01) ? MapperBase::MirrorMode::HORIZONTAL : MapperBase::MirrorMode::VERTICAL);
        break;
    }
    RecomputeMappings();
}

void MMC2Mapper::RecomputePrgMappings() {
    // PRG Setup:
    // $8000: Selectable
    // $A000, $C000, $E000: Fixed to last three 8KB banks
    MapperBase::SetPrgPage(0, prg_bank_select);
    MapperBase::SetPrgPage(1, prgBank8kCount - 3);
    MapperBase::SetPrgPage(2, prgBank8kCount - 2);
    MapperBase::SetPrgPage(3, prgBank8kCount - 1);
}

void MMC2Mapper::RecomputeChrMappings() {
    // CHR Setup:
    // Page 0 (0x0000) uses latch_0 to pick between two registers
    // Page 1 (0x1000) uses latch_1 to pick between two registers
    MapperBase::SetChrPage(0, chr_bank_0[latch_0]);
    MapperBase::SetChrPage(1, chr_bank_1[latch_1]);
}

void MMC2Mapper::Serialize(Serializer& serializer) {
    MapperBase::Serialize(serializer);
    serializer.Write(prg_bank_select);
    serializer.Write(chr_bank_0);
    serializer.Write(chr_bank_1);
    serializer.Write(latch_0);
    serializer.Write(latch_1);
}

void MMC2Mapper::Deserialize(Serializer& serializer) {
    MapperBase::Deserialize(serializer);
    serializer.Read(prg_bank_select);
    serializer.Read(chr_bank_0);
    serializer.Read(chr_bank_1);
    serializer.Read(latch_0);
    serializer.Read(latch_1);
    RecomputeMappings();
}