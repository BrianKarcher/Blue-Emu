#pragma once
#include <stdint.h>
#include <array>
#include "SharedContext.h"

class PPU;
class Bus;
class A12Mapper;
class Serializer;

#define DOTS_PER_SCANLINE 340
#define SCANLINES_PER_FRAME 261
#define DISABLE_CLOCK

class RendererLoopy
{
public:
    // Sprite data for current scanline
    typedef struct Sprite {
        uint8_t x;
        uint8_t y;
        uint8_t tileIndex;
        uint8_t attributes;
        bool isSprite0;
    } Sprite;

    int m_scanline = 0; // Current PPU scanline (0-261)
    int dot = 0;

    // Loopy register structure (15-bit VRAM address)
    // yyy NN YYYYY XXXXX
    // ||| || ||||| +++++-- coarse X scroll
    // ||| || +++++-------- coarse Y scroll
    // ||| ++-------------- nametable select
    // +++----------------- fine Y scroll

	// TODO : Use bitfields for LoopyRegister based on the above layout
    typedef struct {
        uint16_t coarse_x : 5;  // Coarse X scroll (0-31)
        uint16_t coarse_y : 5;  // Coarse Y scroll (0-31)
        uint16_t nametable_x : 1;  // Nametable X bit
        uint16_t nametable_y : 1;  // Nametable Y bit
        uint16_t fine_y : 3;    // Fine Y scroll (0-7)
        uint16_t unused : 1;    // Always 0
    } LoopyRegister;

    typedef struct {
        LoopyRegister v;  // Current VRAM address (15 bits)
        LoopyRegister t;  // Temporary VRAM address (15 bits) / address of top-left tile
        uint8_t x;        // Fine X scroll (3 bits)
        bool w;           // Write latch (first/second write toggle)
    } PPURegisters;

    // Shift registers for rendering
    typedef struct {
        uint16_t pattern_lo_shift;  // 16-bit shift register for pattern low bits
        uint16_t pattern_hi_shift;  // 16-bit shift register for pattern high bits
        uint16_t attr_lo_shift;      // 8-bit shift register for attribute low bit
        uint16_t attr_hi_shift;      // 8-bit shift register for attribute high bit
    } ShiftRegisters;

    // Tile fetching structure
    typedef struct {
        uint8_t coarseX;
        uint8_t coarseY;
        uint8_t nametable_byte;  // Tile index
        uint8_t attribute_byte;  // Palette info
        uint8_t pattern_low;     // Low bit plane
        uint8_t pattern_high;    // High bit plane
    } TileFetch;

    PPURegisters loopy;
    ShiftRegisters m_shifts;

    RendererLoopy(SharedContext& ctx);
    void initialize(PPU* ppu);
    void reset();
    void setPPUCTRL(uint8_t value);
    void setPPUMask(uint8_t value) { ppumask = value; }
    void writeScroll(uint8_t value);
    uint8_t getScrollY();
    uint8_t getScrollX();
    void ppuWriteAddr(uint8_t value);
    uint16_t getPPUAddr();
    void ppuReadStatus();
    void ppuIncrementX();
    void ppuIncrementFineY();
    void ppuCopyX();
    void ppuCopyY();
    uint16_t ppuGetVramAddr();
    void ppuIncrementVramAddr(uint8_t increment);
    void clock(uint32_t* buffer);
    bool isFrameComplete() { return m_frameComplete; }
    void setFrameComplete(bool complete) { m_frameComplete = complete; }
    uint16_t get_attribute_address(LoopyRegister& regV);

    void setMapper(A12Mapper* mapper) {
        m_mapper = mapper;
    }
    bool m_frameTick = false;

    void Serialize(Serializer& serializer);
	void Deserialize(Serializer& serializer);

private:
    Bus* m_bus;
    PPU* m_ppu;
    A12Mapper* m_mapper;
    SharedContext& context;
    // Overflow can only be set once per frame
    bool hasOverflowBeenSet = false;
    bool hasSprite0HitBeenSet = false;
    bool m_frameComplete = false;
    uint64_t _frameCount = 0;

    struct SpriteRenderData {
        uint8_t x;           // X position on screen
        uint8_t colorIndex;  // 0–3 (0 = transparent)
        uint8_t palette;     // Lower 2 bits of attributes
        bool    behindBg;    // Priority bit
        bool    isZero;      // Is this sprite 0?
		bool	valid;       // Is this a valid sprite pixel (used for sprite 0 hit detection)
    };

	std::array<SpriteRenderData, 256> spriteLineBuffer;  // Places all sprites for current scanline so we only calculate it once
    void prepareSpriteLine(int y);
    
    uint8_t ppumask = 0;
    std::array<Sprite, 8> secondaryOAM{};

    // Tile info
    TileFetch tile;

    // Fetchers / Shift Registers for the NEXT line
    uint8_t spritePatternTableLow[8];
    uint8_t spritePatternTableHigh[8];
    uint16_t spritePatternAddrLow[8];
    uint16_t spritePatternAddrHigh[8];

    void evaluateSprites(int screenY, std::array<Sprite, 8>& newOam);
    uint8_t get_pixel();
    inline void ApplyColorEmphasis(uint32_t& finalColor);
    void renderPixel(uint32_t* buffer);
    void renderPixelBackground(uint32_t* buffer);

    // Internal helpers
    inline bool renderingEnabled() const { return (ppumask & 0x18) != 0; } // bg or sprites
    inline bool bgEnabled() const { return (ppumask & 0x08) != 0; }
    inline bool spriteEnabled() const { return (ppumask & 0x10) != 0; }
    void fetch_tile_data(TileFetch* tile, uint8_t pattern_table_base);
    void load_shift_registers();
    void shift_registers();
    uint8_t get_attribute_byte();
    uint8_t get_palette_from_attribute(uint8_t attr, uint8_t coarse_x, uint8_t coarse_y);
};