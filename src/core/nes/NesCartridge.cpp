#include "NesCartridge.h"
#include <string>
#include "NesBus.h"
#include "INESLoader.h"
#include "NROM.h"
#include "MapperBase.h"
#include "MMC1.h"
#include "MMC3.h"
#include "NesCpu.h"
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include "UxROMMapper.h"
#include "AxROMMapper.h"
#include "SharedContext.h"
#include "CNROM.h"
#include "MMC2Mapper.h"
#include "MemoryBuffer.h"
#include "DxROM.h"

#include <iostream>
#include <vector>
#include <string>
#include <zip.h> // The libzip header

std::vector<uint8_t> NesCartridge::ReadNesFromZip(const std::string& zipPath) {
    int err = 0;

    zip* archive = zip_open(zipPath.c_str(), 0, &err);
    if (!archive) {
        std::cerr << "Failed to open zip file. Error code: " << err << std::endl;
        return std::vector<uint8_t>();
    }

    zip_int64_t numEntries = zip_get_num_entries(archive, 0);
    if (numEntries == 0) {
        std::cerr << "No contents inside Zip file." << err << std::endl;
        return std::vector<uint8_t>();
    }

    zip_stat_t fileStat;
    zip_stat_init(&fileStat);
    // Load whatever is the first file.
	// TODO : Loop through entries to find .nes file
    const char* name = zip_get_name(archive, 0, 0);
    if (zip_stat(archive, name, 0, &fileStat) != 0) {
        std::cerr << "Could not find " << name << " in archive." << std::endl;
        zip_close(archive);
        return std::vector<uint8_t>();
    }

    std::vector<uint8_t> buffer(fileStat.size);

    zip_file* internalFile = zip_fopen(archive, name, 0);
    if (!internalFile) {
        std::cerr << "Failed to open internal file for reading." << std::endl;
        zip_close(archive);
        return std::vector<uint8_t>();
    }

    zip_fread(internalFile, buffer.data(), fileStat.size);
    zip_fclose(internalFile);
    zip_close(archive);

    std::cout << "Successfully loaded " << buffer.size() << " bytes into the buffer." << std::endl;

    // Optional: Verify NES Header (First 3 bytes should be 'N', 'E', 'S')
    if (buffer.size() > 4 && buffer[0] == 'N' && buffer[1] == 'E' && buffer[2] == 'S') {
        std::cout << "Valid iNES header detected!" << std::endl;
    }

    return buffer;
}

NesCartridge::NesCartridge(SharedContext& ctx, NesCpu& c) : cpu(c), ctx(ctx) {
	m_isLoaded = false;
}

std::filesystem::path getAppFolderPath()
{
    wchar_t buf[MAX_PATH];
	// This is a Windows-specific way to get the executable path
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::filesystem::path(buf).parent_path();
}

std::filesystem::path NesCartridge::getAndEnsureSavePath() {
    std::filesystem::path appFolder = getAppFolderPath();
    std::filesystem::path sramPath = appFolder / "Save";
    std::filesystem::create_directories(sramPath);
    return sramPath;
}

void NesCartridge::loadSRAM() {
    mapper->m_prgRamData.resize(0x2000);
    if (!isBatteryBacked) {
        return;
    }
    std::filesystem::path appFolder = getAndEnsureSavePath();
    std::filesystem::path sramFilePath = appFolder / (fileName + L".sav");
    std::ifstream in(sramFilePath, std::ios::binary | std::ios::ate);
    if (!in)
		return;

    std::streamsize size = in.tellg();
    in.seekg(0, std::ios::beg);

    if (!in.read(reinterpret_cast<char*>(mapper->m_prgRamData.data()), size))
        throw std::runtime_error("Failed to read file");
}

void NesCartridge::saveSRAM() {
    if (!isBatteryBacked)
		return;
    std::filesystem::path appFolder = getAndEnsureSavePath();
	std::filesystem::path sramFilePath = appFolder / (fileName + L".sav");
    std::ofstream out(sramFilePath, std::ios::binary);
    if (!out)
        throw std::runtime_error("Failed to open output file");

    out.write(reinterpret_cast<const char*>(mapper->m_prgRamData.data()), mapper->m_prgRamData.size());
}

void NesCartridge::unload() {
    saveSRAM();
    if (mapper) {
        mapper->m_prgRomData.clear();
        mapper->m_chrData.clear();
        mapper->m_prgRamData.clear();
		mapper->shutdown();
        delete mapper;
        mapper = nullptr;
    }
    m_isLoaded = false;
}

std::vector<uint8_t> NesCartridge::LoadFileToBuffer(const std::string& path) {
    // Check if it's an archive by extension
    if (path.find(".7z") != std::string::npos || path.find(".zip") != std::string::npos) {
        return ReadNesFromZip(path);
    }

    // Standard file loading
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return {};

    size_t size = file.tellg();
    std::vector<uint8_t> buffer(size);
    file.seekg(0, std::ios::beg);
    file.read((char*)buffer.data(), size);
    return buffer;
}

void NesCartridge::LoadROM(const std::string& filePath) {
    INESLoader ines;
    std::filesystem::path filepath(filePath);
    ines_file_t inesFile;
	fileName = filepath.stem().wstring();
    std::vector<uint8_t> rawData = LoadFileToBuffer(filePath);
    if (rawData.empty()) {
        MessageBoxA(NULL, "File not found or empty.", "Error", MB_OK | MB_ICONERROR);
        return;
    }

    // Wrap it in our memory helper
    MemoryBuffer stream(rawData);
    ines.load_data_from_ines(stream, inesFile);

    isBatteryBacked = inesFile.header.flags6 & FLAG_6_BATTERY_BACKED;

    uint8_t mapperNum = inesFile.header.flags6 >> 4;
    SetMapper(mapperNum, inesFile);
	mapper->initialize(inesFile);
	mapper->register_memory(*m_bus);
    loadSRAM();
    
    m_isLoaded = true;
}

// TODO - This could be refactored into a factory.
void NesCartridge::SetMapper(uint8_t value, ines_file_t& inesFile) {
    if (mapper) {
        mapper->shutdown();
        delete mapper;
        mapper = nullptr;
    }

    switch (value) {
    case 0:
        mapper = new NROM(this);
        break;
    case 1:
        mapper = new MMC1(this, cpu);
        break;
    case 2:
		mapper = new UxROMMapper(*m_bus, inesFile.header.prg_rom_size, inesFile.header.chr_rom_size);
        break;
    case 3:
		mapper = new CNROM();
        break;
    case 4:
        mapper = new MMC3(*m_bus, inesFile.header.prg_rom_size, inesFile.header.chr_rom_size);
        break;
    case 7:
        mapper = new AxROMMapper(this, inesFile.header.prg_rom_size);
        break;
    case 9:
		mapper = new MMC2Mapper(*m_bus, inesFile.header.prg_rom_size, inesFile.header.chr_rom_size);
        break;
    case 206:
        mapper = new DxROM();
        break;
    default:
        mapper = new NROM(this);
        break;
    }
}

uint8_t NesCartridge::ReadPRGRAM(uint16_t address) {
    return mapper->m_prgRamData[address - 0x6000];
}

void NesCartridge::WritePRGRAM(uint16_t address, uint8_t data) {
    mapper->m_prgRamData[address - 0x6000] = data;
}

// ---------------- Debug helper ----------------
inline void NesCartridge::dbg(const wchar_t* fmt, ...) {
    wchar_t buf[512];
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(buf, sizeof(buf) / sizeof(buf[0]), _TRUNCATE, fmt, args);
    va_end(args);
}

void NesCartridge::SetPrgRamEnabled(bool enable) {
    LOG(L"Setting PrgRamEnabled to %d\n", enable);
    isPrgRamEnabled = enable;
}

bool NesCartridge::isLoaded() {
	return m_isLoaded;
}