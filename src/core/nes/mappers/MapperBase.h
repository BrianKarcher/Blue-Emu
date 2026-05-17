#pragma once
#include "Mapper.h"
#include <array>
#include <cstdint>
#include <stdarg.h>

//#define MAPPERDEBUG

#if defined(_DEBUG) && defined(MAPPERDEBUG)
#define LOG(...) dbg(__VA_ARGS__)
#else
#define LOG(...) do {} while(0) // completely removed by compiler
#endif

// TODO : Integrate common mapper functionality here
class MapperBase : public Mapper {
	// Converting all PRG and CHR mapping to use pages.
	// For simplicity across all mappers. All mappers have window sizes >= 256 bytes.
	// This also increases performance by reducing the number of calculations needed for each read/write.

public:
	enum MirrorMode {
		HORIZONTAL = 0,
		VERTICAL = 1,
		SINGLE_LOWER = 2,
		SINGLE_UPPER = 3,
		FOUR_SCREEN = 4
	};
	uint8_t* _prgPages[0x100];
	uint16_t _prgPageSize = 0;
	uint16_t _prgRomSize = 0;
	uint8_t _prgPageCount = 0;
	uint8_t* _ppuPages[0x100];
	bool _isPpuPageWritable[0x100];
	uint16_t _chrPageSize = 0;
	uint16_t _chrRomSize = 0;
	uint8_t _chrPageCount = 0;
	const uint16_t _nametablePageSize = 0x400;

	inline void dbg(const wchar_t* fmt, ...) const;
	virtual void RecomputeMappings();
	virtual void RecomputePrgMappings() = 0;
	virtual void RecomputeChrMappings() = 0;
	virtual void RecomputeMirrorModeMapping();
	virtual void initialize(ines_file_t& data) override;
	void SetPrgPageSize(uint16_t pageSize);
	void SetPrgPage(uint16_t pageIndex, uint8_t bank);
	void SetPrgRange(uint16_t startInclusive, uint16_t endExclusive, uint32_t bankOffset);

	void SetChrPageSize(uint16_t pageSize);
	void SetChrPage(uint16_t pageIndex, uint8_t bank);
	void SetNametablePage(uint8_t virtualIndex, uint8_t physicalIndex);
	void SetChrRange(uint16_t startInclusive, uint16_t endExclusive, uint8_t* source, uint32_t bankOffset);

	inline uint8_t readPRGROM(uint16_t addr) const {
		addr -= 0x8000;
		return _prgPages[addr >> 8][addr & 0xFF];
	}

	void writePRGROM(uint16_t address, uint8_t data, uint64_t currentCycle);
	virtual inline uint8_t readCHR(uint16_t addr) {
		return _ppuPages[addr >> 8][addr & 0xFF];
	}

	MapperBase::MirrorMode GetMirrorMode();
	void SetMirrorMode(MirrorMode mirrorMode);

	void writeCHR(uint16_t addr, uint8_t data);
	void shutdown();

	uint16_t _nametableRamSize = 0x800; // Default 2 KB nametable RAM size
	std::vector<uint8_t> _vram; // 2 KB VRAM used to hold nametables. Some mappers may override this.

	virtual void Serialize(Serializer& serializer) override;
	virtual void Deserialize(Serializer& serializer) override;
private:
	MirrorMode m_mirrorMode;
};