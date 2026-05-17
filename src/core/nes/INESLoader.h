#pragma once
#include <stdint.h>
#include <string>

#define FLAG_6_BATTERY_BACKED 0x02
#define FLAG_6_NAMETABLE_LAYOUT 0x08
class MemoryBuffer;

// iNES header structure
typedef struct {
    char signature[4];      // "NES\x1A"
    uint8_t prg_rom_size;   // Size of PRG ROM in 16 KB units
    uint8_t chr_rom_size;   // Size of CHR ROM in 8 KB units
    uint8_t flags6;         // Mapper, mirroring, battery, trainer
    uint8_t flags7;         // Mapper, VS/Playchoice, NES 2.0
    uint8_t flags8;         // PRG-RAM size (rarely used)
    uint8_t flags9;         // TV system (rarely used)
    uint8_t flags10;        // TV system, PRG-RAM presence (unofficial)
    uint8_t padding[5];     // Unused padding
} ines_header_t;

// PRG-ROM data structure
typedef struct {
    uint8_t* data;          // Raw PRG-ROM data
    size_t size;            // Size in bytes
} prg_rom_data_t;

// CHR-ROM data structure
typedef struct {
    uint8_t* data;          // Raw CHR-ROM data
    size_t size;            // Size in bytes
} chr_rom_data_t;

typedef struct {
    uint8_t* data;          // Raw iNES file data
    size_t size;            // Size in bytes
    ines_header_t header;   // Parsed iNES header
	// TODO : Support banks for mapping
	prg_rom_data_t* prg_rom; // PRG-ROM data
    chr_rom_data_t* chr_rom; // CHR-ROM data
    uint8_t* trainer_data;  // Trainer data (if present)
    bool has_trainer;       // Flag indicating if trainer is present
    bool is_valid;          // Flag indicating if the iNES file is valid
	bool is_nes2;           // Flag indicating if this is a NES 2.0 file
} ines_file_t;

class INESLoader
{
public:
    void load_data_from_ines(MemoryBuffer stream, ines_file_t& ines_file);

private:
    bool validate_ines_header(const ines_header_t* header);
};