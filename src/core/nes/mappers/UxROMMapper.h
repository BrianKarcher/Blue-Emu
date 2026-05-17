#pragma once
#include <stdint.h>
#include <array>
#include "MapperBase.h"

class NesBus;
class NesCartridge;
class CPU;
class RendererLoopy;

#ifdef _DEBUG
#define LOG(...) dbg(__VA_ARGS__)
#else
#define LOG(...) do {} while(0) // completely removed by compiler
#endif

//#define UXROMDEBUG

class UxROMMapper : public MapperBase
{
public:
	UxROMMapper(NesBus& bus, uint8_t prgRomSize, uint8_t chrRomSize);

	void writeRegister(uint16_t addr, uint8_t val, uint64_t currentCycle);
	void RecomputePrgMappings() override;
	void RecomputeChrMappings() override;
	void shutdown() {}
	void Serialize(Serializer& serializer) override;
	void Deserialize(Serializer& serializer) override;

private:
	inline void dbg(const wchar_t* fmt, ...);
	// Mapper state
	uint8_t prg_bank_select;  // Selected 16KB bank for $8000-$BFFF
	uint8_t prgBank16kCount;

	NesCartridge* cart;
	NesBus& bus;
};