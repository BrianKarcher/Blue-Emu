#pragma once
#include <cstdint>
#include "Mapper.h"
#include "MapperBase.h"
#include "INESLoader.h"

//#define MMC1DEBUG

#if defined(_DEBUG) && defind(MMC1DEBUG)
#define LOG(...) dbg(__VA_ARGS__)
#else
#define LOG(...) do {} while(0) // completely removed by compiler
#endif

#define CHR_BANK_SIZE 0x1000 // 4k
#define PRG_BANK_SIZE 0x4000 // 16k

enum BoardType {
	SAROM,
	SNROM,
	SUROM,
	GenericMMC1
};

class Cartridge;
class CPU;

class MMC1 : public MapperBase
{
public:
	MMC1(Cartridge* cartridge, CPU& cpu);
	void writeRegister(uint16_t addr, uint8_t val, uint64_t currentCycle);
	void RecomputePrgMappings() override;
	void RecomputeChrMappings() override;
	void initialize(ines_file_t& data) override;

	inline void dbg(const wchar_t* fmt, ...) const;
	BoardType boardType;
	uint8_t prgBankReg = 0;
	uint8_t suromPrgOuterBank;
	void shutdown() { }
	void Serialize(Serializer& serializer) override;
	void Deserialize(Serializer& serializer) override;

private:
	//uint8_t nametableMode;
	//uint8_t prgROMMode;
	//uint8_t chrROMMode;
	uint8_t controlReg = 0x0C;   // default after reset usually 0x0C (mirroring = 0x0, PRG mode = 3)
	uint8_t shiftRegister;
	void processShift(uint16_t addr, uint8_t val);
	uint8_t prgBank16kCount;
	uint8_t chrBankCount;
	uint8_t chrBank0Reg = 0;
	uint8_t chrBank1Reg = 0;
	
	Cartridge* cartridge;
	CPU& cpu;
	//ines_file_t* inesFile;
	uint64_t lastWriteCycle = 0;
	bool debug = false;
};