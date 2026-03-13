#pragma once
#include <cstdint>
#include <string>
#include <stdint.h>
#include <array>
#include <vector>
#include "MapperBase.h"
#include "INESLoader.h"
#include <filesystem>

#ifdef _DEBUG
#define LOG(...) dbg(__VA_ARGS__)
#else
#define LOG(...) do {} while(0) // completely removed by compiler
#endif

class NesCpu;
class NesBus;
class SharedContext;

class Cartridge
{
public:
	Cartridge(SharedContext& ctx, NesCpu& c);
	void connectNesBus(NesBus* bus) { m_bus = bus; }

	void LoadROM(const std::string& filePath);
	// Map a NesPpu address ($2000–$2FFF) to actual VRAM offset (0–0x7FF)
	uint16_t MirrorNametable(uint16_t addr);
	uint8_t ReadPRGRAM(uint16_t address);
	void WritePRGRAM(uint16_t address, uint8_t data);
	void SetPrgRamEnabled(bool enable);
	bool isPrgRamEnabled = true;
	void SetMapper(uint8_t value, ines_file_t& inesFile);
	void unload();
	bool isLoaded();
	MapperBase* mapper;
	SharedContext& ctx;
	std::wstring fileName;
	std::filesystem::path getAndEnsureSavePath();
private:
	NesBus* m_bus;
	NesCpu& cpu;
	std::vector<uint8_t> ReadNesFromZip(const std::string& zipPath);
	std::vector<uint8_t> LoadFileToBuffer(const std::string& path);
	void loadSRAM();
	void saveSRAM();
	bool isBatteryBacked = false;
	bool m_isLoaded;
	inline void dbg(const wchar_t* fmt, ...);
};