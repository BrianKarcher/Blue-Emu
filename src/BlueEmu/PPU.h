#pragma once
#include <Windows.h>
#include <stdint.h>
#include <array>
#include <utility>
#include "SharedContext.h"
#include "MemoryMapper.h"

#ifdef _DEBUG
#define LOG(...) dbg(__VA_ARGS__)
#else
#define LOG(...) do {} while(0) // completely removed by compiler
#endif

#define NMI_ENABLE 0x80

#define NAMETABLE_WIDTH 32
#define NAMETABLE_HEIGHT 30
#define TILE_SIZE 8
#define PPUCTRL 0x2000
#define PPUMASK 0x2001
#define PPUSTATUS 0x2002
#define OAMADDR 0x2003
#define OAMDATA 0x2004
#define PPUSCROLL 0x2005
#define PPUADDR 0x2006
#define PPUDATA 0x2007
#define PPUCTRL_SPRITESIZE 0x20
#define PPUCTRL_INCREMENT 0x04
#define PPUCTRL_SPRITE_PATTERN_TABLE 0x08
#define PPUCTRL_BACKGROUND_PATTERN_TABLE 0x10
#define PPUSTATUS_VBLANK 0x80
#define PPUSTATUS_SPRITE0_HIT 0x40
#define PPUSTATUS_SPRITE_OVERFLOW 0x20
#define PPUMASK_GRAYSCALE 0x01
#define PPUMASK_BACKGRONDLEFT 0x02
#define PPUMASK_SPRITELEFT 0x04
#define PPUMASK_BACKGROUNDENABLED 0x08
#define PPUMASK_SPRITEENABLED 0x10
#define PPUMASK_EMPHASIZERED 0x20
#define PPUMASK_EMPHASIZEGREEN 0x40
#define PPUMASK_EMPHASIZEBLUE 0x80
#define PPUMASK_RENDERINGEITHER PPUMASK_BACKGROUNDENABLED | PPUMASK_SPRITEENABLED

//#define PPUDEBUG

// RGB
// NES color palette (64 colors)
//static constexpr uint32_t m_nesPalette[64] = {
//	0x666666, 0x002A88, 0x1412A7, 0x3B00A4, 0x5C007E, 0x6E0040, 0x6C0600, 0x561D00,
//	0x333500, 0x0B4800, 0x005200, 0x004F08, 0x00404D, 0x000000, 0x000000, 0x000000,
//	0xADADAD, 0x155FD9, 0x4240FF, 0x7527FE, 0xA01ACC, 0xB71E7B, 0xB53120, 0x994E00,
//	0x6B6D00, 0x388700, 0x0C9300, 0x008F32, 0x007C8D, 0x000000, 0x000000, 0x000000,
//	0xFFFEFF, 0x64B0FF, 0x9290FF, 0xC676FF, 0xF36AFF, 0xFE6ECC, 0xFE8170, 0xEA9E22,
//	0xBCBE00, 0x88D800, 0x5CE430, 0x45E082, 0x48CDDE, 0x4F4F4F, 0x000000, 0x000000,
//	0xFFFEFF, 0xC0DFFF, 0xD3D2FF, 0xE8C8FF, 0xFBC2FF, 0xFEC4EA, 0xFECCC5, 0xF7D8A5,
//	0xE4E594, 0xCFEF96, 0xBDF4AB, 0xB3F3CC, 0xB5EBF2, 0xB8B8B8, 0x000000, 0x000000
//};

// RGBA
// NES color palette (64 colors)
//static constexpr uint32_t m_nesPalette[64] = {
//	0x666666FF, 0x002A88FF, 0x1412A7FF, 0x3B00A4FF, 0x5C007EFF, 0x6E0040FF, 0x6C0600FF, 0x561D00FF,
//	0x333500FF, 0x0B4800FF, 0x005200FF, 0x004F08FF, 0x00404DFF, 0x000000FF, 0x000000FF, 0x000000FF,
//	0xADADADFF, 0x155FD9FF, 0x4240FFFF, 0x7527FEFF, 0xA01ACCFF, 0xB71E7BFF, 0xB53120FF, 0x994E00FF,
//	0x6B6D00FF, 0x388700FF, 0x0C9300FF, 0x008F32FF, 0x007C8DFF, 0x000000FF, 0x000000FF, 0x000000FF,
//	0xFFFEFFFF, 0x64B0FFFF, 0x9290FFFF, 0xC676FFFF, 0xF36AFFFF, 0xFE6ECCFF, 0xFE8170FF, 0xEA9E22FF,
//	0xBCBE00FF, 0x88D800FF, 0x5CE430FF, 0x45E082FF, 0x48CDDEFF, 0x4F4F4FFF, 0x000000FF, 0x000000FF,
//	0xFFFEFFFF, 0xC0DFFFFF, 0xD3D2FFFF, 0xE8C8FFFF, 0xFBC2FFFF, 0xFEC4EAFF, 0xFECCC5FF, 0xF7D8A5FF,
//	0xE4E594FF, 0xCFEF96FF, 0xBDF4ABFF, 0xB3F3CCFF, 0xB5EBF2FF, 0xB8B8B8FF, 0x000000FF, 0x000000FF
//};

// ARGB
static constexpr uint32_t m_nesPalette[64] = {
	0xFF666666, 0xFF002A88, 0xFF1412A7, 0xFF3B00A4, 0xFF5C007E, 0xFF6E0040, 0xFF6C0600, 0xFF561D00,
	0xFF333500, 0xFF0B4800, 0xFF005200, 0xFF004F08, 0xFF00404D, 0xFF000000, 0xFF000000, 0xFF000000,
	0xFFADADAD, 0xFF155FD9, 0xFF4240FF, 0xFF7527FE, 0xFFA01ACC, 0xFFB71E7B, 0xFFB53120, 0xFF994E00,
	0xFF6B6D00, 0xFF388700, 0xFF0C9300, 0xFF008F32, 0xFF007C8D, 0xFF000000, 0xFF000000, 0xFF000000,
	0xFFFFFEFF, 0xFF64B0FF, 0xFF9290FF, 0xFFC676FF, 0xFFF36AFF, 0xFFFE6ECC, 0xFFFE8170, 0xFFEA9E22,
	0xFFBCBE00, 0xFF88D800, 0xFF5CE430, 0xFF45E082, 0xFF48CDDE, 0xFF4F4F4F, 0xFF000000, 0xFF000000,
	0xFFFFFEFF, 0xFFC0DFFF, 0xFFD3D2FF, 0xFFE8C8FF, 0xFFFBC2FF, 0xFFFEC4EA, 0xFFFECCC5, 0xFFF7D8A5,
	0xFFE4E594, 0xFFCFEF96, 0xFFBDF4AB, 0xFFB3F3CC, 0xFFB5EBF2, 0xFFB8B8B8, 0xFF000000, 0xFF000000
};

class Bus;
class Core;
class RendererLoopy;
class A12Mapper;
class Nes;
class Serializer;
class DebuggerContext;

class PPU : public MemoryMapper
{
public:
	PPU(SharedContext& ctx, Nes& nes);
	~PPU();
	void register_memory(Bus& bus);
	void initialize();
	void connectBus(Bus* bus) { this->bus = bus; }
	RendererLoopy* renderer;
	void reset();
	void step();
	uint8_t read(uint16_t address);
	uint8_t peek(uint16_t address);
	void write(uint16_t address, uint8_t value);
	inline void write_register(uint16_t addr, uint8_t value);
	void writeOAM(uint16_t addr, uint8_t val);
	inline uint8_t read_register(uint16_t addr);
	uint8_t peek_register(uint16_t addr);
	uint8_t PeekVRAM(uint16_t addr);
	uint8_t ReadVRAM(uint16_t addr);
	uint8_t GetScrollX() const;
	uint8_t GetScrollY() const;

	void setMapper(A12Mapper* mapper) {
		m_mapper = mapper;
	}
		
	void set_hwnd(HWND hwnd);

	std::array<uint8_t, 0x100> oam; // 256 bytes OAM (sprite memory)
	uint8_t oamAddr;
	Bus* bus;
	A12Mapper* m_mapper;
	Nes& nes;

	void Clock();
	
	std::array<uint8_t, 32> paletteTable; // 32 bytes palette table
	uint16_t GetVRAMAddress() const;
	void SetVRAMAddress(uint16_t addr);
	uint8_t GetPPUStatus() const { return m_ppuStatus; }
	uint8_t GetPPUCtrl() const { return m_ppuCtrl; }
	
	uint8_t m_ppuMask = 0;
	uint8_t m_ppuStatus = 0;
	uint8_t m_ppuCtrl = 0;
	void get_palette(uint8_t paletteIndex, std::array<uint32_t, 4>& colors);
	uint8_t get_tile_pixel_color_index(uint8_t tileIndex, uint8_t pixelInTileX, uint8_t pixelInTileY, bool isSprite, bool isSecondSprite);
	bool isFrameComplete();
	void setFrameComplete(bool complete);
	void SetPPUStatus(uint8_t flag);
	uint16_t GetBackgroundPatternTableBase() const {
		return (m_ppuCtrl & 0x10) ? 0x1000 : 0x0000; // Bit 4 of PPUCTRL
	};
	uint16_t GetSpritePatternTableBase(uint8_t tileId) const {
		if (!(m_ppuCtrl & PPUCTRL_SPRITESIZE)) {
			return (m_ppuCtrl & 0x08) ? 0x1000 : 0x0000; // Bit 3 of PPUCTRL
		}
		else {
			return (tileId & 1) == 1 ? 0x1000 : 0x000;
		}
	}
	void setBuffer(uint32_t* buf) { buffer = buf; }
	void UpdateState();
	bool isFrameTicked();

	void Serialize(Serializer& serializer);
	void Deserialize(Serializer& serializer);

private:
	SharedContext& context;
	DebuggerContext* dbgContext;
	uint32_t* buffer;
	bool is_failure = false;

	uint8_t ppuDataBuffer = 0; // Internal buffer for PPUDATA reads

	void write_vram(uint16_t addr, uint8_t value);
	void performDMA(uint8_t page);
	
	//void render_nametable();
	inline void dbg(const wchar_t* fmt, ...);
	void clearBuffer(uint32_t* buffer);
};