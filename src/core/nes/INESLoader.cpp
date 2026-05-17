#include "INESLoader.h"
#include <cstdlib>
#include <string.h>
#include <Windows.h>
#include "MemoryBuffer.h"

// Function to validate iNES header
bool INESLoader::validate_ines_header(const ines_header_t* header) {
    return (header->signature[0] == 'N' &&
        header->signature[1] == 'E' &&
        header->signature[2] == 'S' &&
        header->signature[3] == 0x1A);
}

// Function to load CHR-ROM data from iNES file
void INESLoader::load_data_from_ines(MemoryBuffer stream, ines_file_t& ines_file) {
    // Read and validate header
    if (stream.read(&ines_file.header, sizeof(ines_header_t)) == 0) {
        printf("Error: Cannot read iNES header\n");
        return;
    }

    if (!validate_ines_header(&ines_file.header)) {
        printf("Error: Invalid iNES header\n");
        return;
    }

    // Calculate sizes
    size_t prg_rom_size = ines_file.header.prg_rom_size * 16384;  // 16KB units
    size_t chr_rom_size = ines_file.header.chr_rom_size * 8192;   // 8KB units
    size_t trainer_size = (ines_file.header.flags6 & 0x04) ? 512 : 0;

    // Skip trainer if present
    if (trainer_size > 0) {
        stream.seek(trainer_size, SEEK_CUR);
    }

    // Allocate memory for PRG-ROM data
    prg_rom_data_t* prg_data = (prg_rom_data_t*)malloc(sizeof(prg_rom_data_t));
    ines_file.prg_rom = prg_data;
    if (!prg_data) {
        printf("Error: Cannot allocate memory for PRG data structure\n");
        return;
    }

    // Read PRG-ROM data
    prg_data->data = (uint8_t*)malloc(prg_rom_size);
    if (stream.read(prg_data->data, prg_rom_size) == 0) {
		MessageBoxA(NULL, "Failed to read PRG-ROM data from the file.", "Error", MB_OK | MB_ICONERROR);
        printf("Error: Cannot read PRG-ROM data\n");
        free(prg_data->data);
        free(prg_data);
        return;
    }
    prg_data->size = prg_rom_size;

    // Allocate memory for CHR-ROM data
    chr_rom_data_t* chr_data = (chr_rom_data_t*)malloc(sizeof(chr_rom_data_t));
	ines_file.chr_rom = chr_data;
    if (!chr_data) {
		MessageBoxA(NULL, "Failed to allocate memory for CHR data structure.", "Error", MB_OK | MB_ICONERROR);
        printf("Error: Cannot allocate memory for CHR data structure\n");
        return;
    }

    if (chr_rom_size == 0) {
        // CHR-RAM
        chr_data->data = nullptr;
    }
    else {
        chr_data->data = (uint8_t*)malloc(chr_rom_size);
        if (!chr_data->data) {
            MessageBoxA(NULL, "Failed to allocate memory for CHR-ROM data.", "Error", MB_OK | MB_ICONERROR);
            printf("Error: Cannot allocate memory for CHR-ROM data\n");
            free(chr_data);
            return;
        }

        // Read CHR-ROM data
        if (stream.read(chr_data->data, chr_rom_size) == 0) {
            MessageBoxA(NULL, "Failed to read CHR-ROM data from the file.", "Error", MB_OK | MB_ICONERROR);
            printf("Error: Cannot read CHR-ROM data\n");
            free(chr_data->data);
            free(chr_data);
            return;
        }
    }

    // Set up structure
    chr_data->size = chr_rom_size;

    // Calculate bitmap dimensions (16x16 tiles for 8KB, 32x32 for 16KB, etc.)
    int num_tiles = chr_rom_size / 16;  // Each tile is 16 bytes
    int tiles_per_row = 16;  // Arrange in 16-tile rows
    int num_rows = (num_tiles + tiles_per_row - 1) / tiles_per_row;

    printf("Successfully loaded CHR-ROM: %zu bytes\n",
        chr_rom_size);

    return;
}