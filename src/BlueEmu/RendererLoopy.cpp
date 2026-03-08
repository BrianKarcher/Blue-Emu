// NES PPU Loopy Registers Implementation
// Based on the loopy register behavior documented by loopy

#include <stdint.h>
#include <stdbool.h>
#include "PPU.h"
#include "A12Mapper.h"
#include "Core.h"
#include "Bus.h"
#include "SharedContext.h"
#include "RendererLoopy.h"
#include "Serializer.h"
#include "PPU.h"

RendererLoopy::RendererLoopy(SharedContext& ctx) : context(ctx) {

}

void RendererLoopy::initialize(PPU* ppu) {
    m_ppu = ppu;
    m_bus = ppu->bus;
}

void RendererLoopy::reset() {
    *(uint16_t*)&loopy.v = 0;
    *(uint16_t*)&loopy.t = 0;
    loopy.x = 0;
    loopy.w = false;
    memset(context.GetBackBuffer(), 0x00, WIDTH * HEIGHT * sizeof(uint32_t));
}

// Write to PPUCTRL ($2000)
void RendererLoopy::setPPUCTRL(uint8_t value) {
    // t: ...GH.. ........ <- d: ......GH
    // G = bit 1, H = bit 0 (nametable select)
    loopy.t.nametable_x = (value >> 0) & 1;
    loopy.t.nametable_y = (value >> 1) & 1;
}

// Write to PPUSCROLL ($2005)
void RendererLoopy::writeScroll(uint8_t value) {
    if (!loopy.w) {
        // First write (X scroll)
        // t: ....... ...HGFED <- d: HGFED...
        // x:              CBA <- d: .....CBA
        loopy.t.coarse_x = value >> 3;
        loopy.x = value & 0x07;
        loopy.w = true;
    }
    else {
        // Second write (Y scroll)
        // t: CBA..HG FED..... <- d: HGFEDCBA
        loopy.t.fine_y = value & 0x07;
        loopy.t.coarse_y = value >> 3;
        loopy.w = false;
    }
}

// Read from PPUSCROLL ($2005)
uint8_t RendererLoopy::getScrollY() {
    return ((loopy.t.coarse_y & 0x1F) << 3) | (loopy.t.fine_y & 0x07);
}

uint8_t RendererLoopy::getScrollX() {
    return ((loopy.t.coarse_x & 0x1F) << 3) | (loopy.x & 0x07);
}

// Write to PPUADDR ($2006)
void RendererLoopy::ppuWriteAddr(uint8_t value) {
    if (!loopy.w) {
        // First write (high byte)
        // t: .FEDCBA ........ <- d: ..FEDCBA
        // t: X...... ........ <- 0
        uint16_t* t_ptr = (uint16_t*)&loopy.t;
        *t_ptr = (*t_ptr & 0x00FF) | ((value & 0x3F) << 8);
        loopy.w = true;
    }
    else {
        // Second write (low byte)
        // t: ....... HGFEDCBA <- d: HGFEDCBA
        // v:                   <- t
        uint16_t* t_ptr = (uint16_t*)&loopy.t;
        *t_ptr = (*t_ptr & 0xFF00) | value;
        *(uint16_t*)&loopy.v = *(uint16_t*)&loopy.t;
        loopy.w = false;
		// MMC3 IRQ handling: clock IRQ counter on PPUADDR write
        // "Should decrement when A12 is toggled via PPUADDR"
        if (m_ppu->m_mapper) {
            m_ppu->m_mapper->ClockIRQCounter(*t_ptr);
        }
    }
}

uint16_t RendererLoopy::getPPUAddr() {
    uint16_t* v_ptr = (uint16_t*)&loopy.v;
	return (*v_ptr & 0b11111111111111); // The address is 14 bits
}

// Read from PPUSTATUS ($2002)
void RendererLoopy::ppuReadStatus() {
    // w:                   <- 0
    loopy.w = false;
}

// Increment coarse X (only when rendering is enabled)
void RendererLoopy::ppuIncrementX() {
    if (loopy.v.coarse_x == 31) {
        loopy.v.coarse_x = 0;
        loopy.v.nametable_x ^= 1;  // Switch horizontal nametable
    }
    else {
        loopy.v.coarse_x++;
    }
}

// Increment Y (only when rendering is enabled)
void RendererLoopy::ppuIncrementFineY() {
    if (loopy.v.fine_y < 7) {
        loopy.v.fine_y++;
    }
    else {
        loopy.v.fine_y = 0;

        if (loopy.v.coarse_y == 29) {
            loopy.v.coarse_y = 0;
            loopy.v.nametable_y ^= 1;  // Switch vertical nametable
        }
        else if (loopy.v.coarse_y == 31) {
            loopy.v.coarse_y = 0;
            // Don't switch nametable (out of bounds)
        }
        else {
            loopy.v.coarse_y++;
        }
    }
}

// Copy horizontal bits from t to v (only when rendering is enabled)
void RendererLoopy::ppuCopyX() {
    // v: ....F.. ...EDCBA <- t: ....F.. ...EDCBA
    loopy.v.nametable_x = loopy.t.nametable_x;
    loopy.v.coarse_x = loopy.t.coarse_x;
}

// Copy vertical bits from t to v (only when rendering is enabled)
void RendererLoopy::ppuCopyY() {
    // v: IHGF.ED CBA..... <- t: IHGF.ED CBA.....
    loopy.v.fine_y = loopy.t.fine_y;
    loopy.v.nametable_y = loopy.t.nametable_y;
    loopy.v.coarse_y = loopy.t.coarse_y;
}

// Get current VRAM address
uint16_t RendererLoopy::ppuGetVramAddr() {
    return *(uint16_t*)&loopy.v & 0x3FFF;  // 14-bit address
}

// Increment VRAM address (after PPUDATA read/write)
void RendererLoopy::ppuIncrementVramAddr(uint8_t increment) {
    uint16_t* v_ptr = (uint16_t*)&loopy.v;
    *v_ptr = (*v_ptr + increment) & 0x7FFF;
}

// Get pixel from shift registers using fine X scroll
uint8_t RendererLoopy::get_pixel() {
    // Select the bit based on fine_x (0-7)
    // Bit 15 is leftmost, bit 0 is rightmost after shifting
    uint16_t mux = 0x8000 >> loopy.x;

    // Extract pixel value (2 bits from pattern planes)
    uint8_t pixel = ((m_shifts.pattern_lo_shift & mux) ? 1 : 0) |
        ((m_shifts.pattern_hi_shift & mux) ? 2 : 0);

    // Extract palette (2 bits from attribute planes)
    uint8_t palette = ((m_shifts.attr_lo_shift & mux) ? 1 : 0) |
        ((m_shifts.attr_hi_shift & mux) ? 2 : 0);

    // Combine into final palette index (0-15)
    if (pixel == 0) return 0;  // Transparent
    return (palette << 2) | pixel;
}

void RendererLoopy::renderPixelBackground(uint32_t* buffer) {
    int x = dot - 1; // visible pixel x [0..255]
    int y = m_scanline; // pixel y [0..239]

    //buffer[y * 256 + x] = 0xFF000000;
    uint8_t bgPaletteIndex = m_ppu->paletteTable[0];
    uint32_t bgColor = m_nesPalette[bgPaletteIndex];
    buffer[y * 256 + x] = bgColor;
}

inline void RendererLoopy::ApplyColorEmphasis(uint32_t& finalColor)
{
    // 4. Color Emphasis Implementation
    // Bits 5, 6, 7 of PPUMASK: R, G, B (NTSC)
    if (ppumask & 0xE0) {
        // Extract RGB components (assuming 0xRRGGBB or 0xBBGGRR format)
        // Adjust shifts based on your m_nesPalette format
        uint8_t r = (finalColor >> 16) & 0xFF;
        uint8_t g = (finalColor >> 8) & 0xFF;
        uint8_t b = finalColor & 0xFF;

        // Emphasis factor (standard NTSC attenuation is ~0.746)
        const float factor = 0.75f;

        if (ppumask & 0x20) { g = (uint8_t)(g * factor); b = (uint8_t)(b * factor); } // Red: Darken G, B
        if (ppumask & 0x40) { r = (uint8_t)(r * factor); b = (uint8_t)(b * factor); } // Green: Darken R, B
        if (ppumask & 0x80) { r = (uint8_t)(r * factor); g = (uint8_t)(g * factor); } // Blue: Darken R, G

        finalColor = (0xFF << 24) | (r << 16) | (g << 8) | b;
    }
}

void RendererLoopy::renderPixel(uint32_t* buffer) {
    int x = dot - 1; // visible pixel x [0..255]
    int y = m_scanline; // pixel y [0..239]
    
    uint8_t bgPaletteIndex = m_ppu->paletteTable[0];
    bool bgOpaque = false;
    // Determine whether or not to show the background in the leftmost 8 pixels of the screen.
    if (bgEnabled() && (dot > 8 || (ppumask & PPUMASK_BACKGRONDLEFT) != 0)) {
        uint8_t pixel = get_pixel();
        bgPaletteIndex = m_ppu->paletteTable[pixel];
        bgOpaque = pixel != 0;
        if (ppumask & PPUMASK_GRAYSCALE) {
            bgPaletteIndex &= 0x30; // Grayscale mode: only use bits 4 and 5 for color
        }
    }

    // Sprite pixel (simplified): we check secondaryOAM sprites for an opaque pixel at current x
	uint8_t spritePaletteIndex = 0;
	uint32_t spriteColor = 0;
	bool spriteOpaque = false;
	bool spritePriorityBehind = false;
	bool spriteIsZero = false;

    uint8_t finalIdx = bgPaletteIndex;
    if (spriteEnabled() && (dot > 8 || (ppumask & PPUMASK_SPRITELEFT) != 0)) {
        bool useSprite = false;

        const auto& spr = spriteLineBuffer[x];
        if (spr.valid) {  // There's a sprite pixel here
            uint8_t palIdx = spr.palette + 4;
            uint8_t sprIdx = m_ppu->paletteTable[0x10 + (spr.palette << 2) + spr.colorIndex];
            if (ppumask & PPUMASK_GRAYSCALE) {
                sprIdx &= 0x30; // Grayscale mode: only use bits 4 and 5 for color
            }
            uint32_t sprColor = m_nesPalette[sprIdx];

            if (spr.isZero && bgOpaque && !hasSprite0HitBeenSet && x < 255) {
                hasSprite0HitBeenSet = true;
                m_ppu->SetPPUStatus(0x40);
            }

            if (!spr.behindBg || !bgOpaque) {
				finalIdx = sprIdx;
                useSprite = true;
            }
        }
    }

	uint32_t finalColor = m_nesPalette[finalIdx];

    ApplyColorEmphasis(finalColor);

    buffer[y * 256 + x] = finalColor;
}

uint16_t RendererLoopy::get_attribute_address(LoopyRegister& regV) {
    // Attribute table starts at +0x3C0 from nametable base
    uint16_t v = (*(uint16_t*)&regV & 0x0FFF);
    uint16_t attr_addr = 0x23C0 | (v & 0x0C00) | ((v >> 4) & 0x38) | ((v >> 2) & 0x07);
    return attr_addr;
}

// Get attribute byte for current tile
uint8_t RendererLoopy::get_attribute_byte() {
    uint16_t attr_addr = get_attribute_address(loopy.v);
    
    // Apply mirroring
    //attr_addr = m_bus->cart->MirrorNametable(attr_addr);
    return m_ppu->ReadVRAM(attr_addr);
}

// Get palette index from attribute byte
uint8_t RendererLoopy::get_palette_from_attribute(uint8_t attr, uint8_t coarse_x, uint8_t coarse_y) {
    // Each attribute byte covers a 4x4 tile area (32x32 pixels)
    // Divided into four 2x2 tile quadrants
    uint8_t quadrant_x = (coarse_x >> 1) & 1;
    uint8_t quadrant_y = (coarse_y >> 1) & 1;
    uint8_t shift = (quadrant_y << 2) | (quadrant_x << 1);
    return (attr >> shift) & 0x03;
}

// Fetch tile data at current v address
void RendererLoopy::fetch_tile_data(TileFetch* tile, uint8_t pattern_table_base) {
    // 1. Fetch nametable byte (tile index)
    uint16_t nametable_addr = 0x2000 | (*(uint16_t*)&loopy.v & 0x0FFF);
    tile->nametable_byte = m_ppu->ReadVRAM(nametable_addr);

    // TODO 2. Fetch attribute byte
    tile->attribute_byte = get_attribute_byte();

    // 3. Fetch pattern table low byte
    // Pattern table address = (pattern_base * 0x1000) + (tile_index * 16) + fine_y
    uint16_t pattern_addr = (pattern_table_base << 12) |
        (tile->nametable_byte << 4) |
        loopy.v.fine_y;
    tile->pattern_low = m_ppu->ReadVRAM(pattern_addr);

    // 4. Fetch pattern table high byte (+8 bytes from low)
    tile->pattern_high = m_ppu->ReadVRAM(pattern_addr + 8);
}

// Load fetched tile into shift registers
void RendererLoopy::load_shift_registers() {
    // Pattern data gets loaded into the lower 8 bits
    m_shifts.pattern_lo_shift = (m_shifts.pattern_lo_shift & 0xFF00) | tile.pattern_low;
    m_shifts.pattern_hi_shift = (m_shifts.pattern_hi_shift & 0xFF00) | tile.pattern_high;

    // Extract palette bits for this tile
    uint8_t palette = get_palette_from_attribute(tile.attribute_byte,
        loopy.v.coarse_x,
        loopy.v.coarse_y);
    // Attribute data gets loaded into the lower 8 bits
    m_shifts.attr_lo_shift = (m_shifts.attr_lo_shift & 0xFF00) | ((palette & 1) ? 0xFF : 0x00);
    m_shifts.attr_hi_shift = (m_shifts.attr_hi_shift & 0xFF00) | ((palette & 2) ? 0xFF : 0x00);
}

// Shift the registers (happens every cycle during rendering)
void RendererLoopy::shift_registers() {
    m_shifts.pattern_lo_shift <<= 1;
    m_shifts.pattern_hi_shift <<= 1;
    m_shifts.attr_lo_shift <<= 1;
    m_shifts.attr_hi_shift <<= 1;
}

/// <summary>
/// Simulates the clock cycle of a PPU (Picture Processing Unit) renderer, handling pixel rendering,
/// background tile fetching, sprite evaluation, and other PPU operations.
/// This function is called once per PPU clock cycle (5.3M/sec) and updates the internal state of the
/// renderer accordingly.
/// This gets called 3 times per CPU cycle. Meaning, I need to make some speed improvements!
/// </summary>
/// <param name="buffer">A pointer to the buffer where rendered pixel data will be written.</param>
void RendererLoopy::clock(uint32_t* buffer) {
    bool rendering = renderingEnabled();
    bool visibleScanline = (m_scanline < 240);
    bool preRenderLine = (m_scanline == 261);

    // 1. Handle VBlank/Idle scanlines first for an early exit
    // Most scanlines in a frame are either visible or VBlank.
    if (m_scanline >= 240 && m_scanline < 261) {
        // NMI and such has to happen for the CPU to function.
        if (m_scanline == 241 && dot == 1) {
            m_ppu->m_ppuStatus |= PPUSTATUS_VBLANK;
            m_frameComplete = true;
            // Inform EmulatorCore of frame completion.
            m_frameTick = true;
            if (m_ppu->m_ppuCtrl & NMI_ENABLE) {
                m_bus->cpu.setNMI(true);
            }
        }
        // This path is HOT. We need every cycle we can get. An else branch is costlier.
		// A bit taboo but sometimes you have to break the rules for performance.
        goto end_of_clock; // Skip all rendering logic
    }

    if (dot == 0) goto end_of_clock;

    // 3. Rendering Hot Path
    if (rendering) {
        // Visible Pixel area
        if (dot <= 256) {
            if (visibleScanline) renderPixel(buffer);
            shift_registers();

            // Combined dot checks.
            if (((dot) & 7) == 0) {
                fetch_tile_data(&tile, m_ppu->GetBackgroundPatternTableBase() == 0x1000 ? 1 : 0);
                load_shift_registers();
                ppuIncrementX();
            }

            if (dot == 256) ppuIncrementFineY();
        }
        // Post-render / Fetch area
        else if (dot >= 321 && dot <= 336) {
            // While in 321..336 we still load shifters and such for the next scanline,
            // but don't render pixels (this is part of the fetch window).
            // shift each dot as well to keep pipeline in sync.
            // This ensures that when we start rendering, we have the correct data in the shifters.
            // With two tiles fetched ahead, we can render the first pixel of the next scanline correctly.
            shift_registers();
            if ((dot & 7) == 0) {
                fetch_tile_data(&tile, m_ppu->GetBackgroundPatternTableBase() == 0x1000);
                load_shift_registers();
                ppuIncrementX();
            }
        }
        // Sprite Eval / Housekeeping area
        else if (dot == 257) {
            ppuCopyX();
            evaluateSprites(m_scanline, secondaryOAM);
            spriteLineBuffer.fill({ 255, 0, 0, false, false, false });  // 255 = no sprite
            prepareSpriteLine(m_scanline);
        }
        else if (dot >= 258 && dot <= 320) {
            prepareSpriteLine(m_scanline);
        }
        if (preRenderLine && dot >= 280 && dot <= 304) {
            // Pre-render only: dots 280..304 copy vertical bits from t to v
            ppuCopyY();
        }
	}
    else {
        // Rendering is OFF
        if (visibleScanline && dot <= 256) renderPixelBackground(buffer);
    }
    
    // 4. Pre-render Line specific state clear
    if (preRenderLine && dot == 1) {
        hasOverflowBeenSet = false;
        hasSprite0HitBeenSet = false;
        m_ppu->m_ppuStatus &= 0x1F; // Clear VBlank, sprite 0 hit, and sprite overflow
        m_frameComplete = false;
        m_bus->cpu.setNMI(false);
        }

    // 5. Odd frame skip
    if (preRenderLine && dot == 339 && (_frameCount & 0x01) && rendering) {
        // We skip a dot every other frame on the pre-render scanline.
        dot = 340;
    }

    end_of_clock:
    if (++dot > DOTS_PER_SCANLINE) {
        dot = 0;
        if (++m_scanline > SCANLINES_PER_FRAME) {
            m_scanline = 0;
            _frameCount++;
        }
    }
}

void RendererLoopy::evaluateSprites(int screenY, std::array<Sprite, 8>& newOam) {
    for (int i = 0; i < 8; ++i) {
        newOam[i] = { 0xFF, 0xFF, 0xFF, 0xFF }; // Initialize to empty sprite
    }
    // Evaluate sprites for this scanline
    int spriteCount = 0;
    for (int i = 0; i < 64; ++i) {
        int spriteY = m_ppu->oam[i * 4]; // Y position of the sprite
        if (spriteY > 0xF0) {
            continue; // Empty sprite slot
        }
        int spriteHeight = (m_ppu->m_ppuCtrl & PPUCTRL_SPRITESIZE) == 0 ? 8 : 16;
        if (screenY >= spriteY && screenY < (spriteY + spriteHeight)) {
            if (spriteCount < 8) {
                // Copy sprite data to new OAM
                newOam[spriteCount].y = m_ppu->oam[i * 4];
                newOam[spriteCount].tileIndex = m_ppu->oam[i * 4 + 1];
                newOam[spriteCount].attributes = m_ppu->oam[i * 4 + 2];
                newOam[spriteCount].x = m_ppu->oam[i * 4 + 3];
                newOam[spriteCount].isSprite0 = (i == 0); // Mark if this is sprite 0
                spriteCount++;
            }
            else {
                // Sprite overflow - more than 8 sprites on this scanline
                if (!hasOverflowBeenSet) {
                    // Set sprite overflow flag only once per frame
                    hasOverflowBeenSet = true;
                    m_ppu->m_ppuStatus |= PPUSTATUS_SPRITE_OVERFLOW;
                }
                break;
            }
        }
    }
}

// Converting into a state machine
void RendererLoopy::prepareSpriteLine(int y) {
    int spriteHeight = (m_ppu->m_ppuCtrl & PPUCTRL_SPRITESIZE) ? 16 : 8;

    // Determine which sprite slot (0-7) we are fetching
    // dot 257-264 = slot 0, 265-272 = slot 1, etc.
    int slot = (dot - 257) / 8;
    int step = (dot - 257) % 8;

    const Sprite& s = secondaryOAM[slot];

    if (s.y <= 0xF0) {
        int i = 0;
    }

    switch (step) {
    case 0: // Garbage NT Read
    case 1: // Garbage NT Read
    case 2: // Garbage AT Read
    case 3: // Garbage AT Read
        // Hardware performs dummy reads here, usually to NT/AT 
        m_ppu->ReadVRAM(0x2000 | s.tileIndex);
        break;

    case 4: { // Fetch Pattern Low Byte
        uint8_t tileId = s.tileIndex;
        uint16_t addrLow;
        int row = (y - s.y);
        bool flipV = s.attributes & 0x80;
        if (flipV) {
            row = (spriteHeight - 1) - row;
        }
        if (spriteHeight == 16) {
            // 8x16: LSB of tile index determines pattern table ($0000 or $1000)
            uint16_t bank = (tileId & 1) * 0x1000;
            tileId &= 0xFE;

            if (s.y >= 0xF0) row = 0; // Force valid row for dummy reads

            if (row >= 8) { // Bottom half of large sprite
                row -= 8;
                tileId |= 1;
            }

            spritePatternAddrLow[slot] = bank + tileId * 16 + row;
        }
        else {
            // 8x8: PPUCTRL Bit 3 determines pattern table
            uint16_t bank = (m_ppu->m_ppuCtrl & 0x08) ? 0x1000 : 0x0000;

            if (s.y >= 0xF0) row = 0;

            spritePatternAddrLow[slot] = bank + (tileId * 16) + row;
        }
        spritePatternTableLow[slot] = m_ppu->ReadVRAM(spritePatternAddrLow[slot]);
    } break;

    case 5: // Read Pattern Low Byte (Cycle 2)
        // Hardware takes 2 cycles for a read.
        break;

    case 6: { // Fetch Pattern High Byte
        // THIS accesses 0x1000 range + 8 bytes
        spritePatternAddrHigh[slot] = spritePatternAddrLow[slot] + 8;
        spritePatternTableHigh[slot] = m_ppu->ReadVRAM(spritePatternAddrHigh[slot]);
    } break;

    case 7: { // Read Pattern High Byte (Cycle 2)
        // Ensure we read the RAM first due to IRQ timing issues, cycle accuracy (needs work!), etc.
        if (s.y >= 0xF0) return;

        int spriteY = s.y;
        int relY = y - spriteY;
        //if (relY < 0 || relY >= (spriteHeight)) continue;

        bool flipV = s.attributes & 0x80;
        bool flipH = s.attributes & 0x40;
        uint8_t palette = s.attributes & 0x03;
        bool behind = s.attributes & 0x20;

        if (flipV) relY = (spriteHeight - 1) - relY;

        // We cache the next line of sprite pixels into a line buffer for quick access during pixel rendering
        for (int x = 0; x < 8; ++x) {
            int screenX = s.x + x;
            if (screenX >= 256) break;

            int bit = flipH ? x : (7 - x);
            uint8_t color = ((spritePatternTableHigh[slot] >> bit) & 1) << 1 |
                ((spritePatternTableLow[slot] >> bit) & 1);

            if (color != 0) {  // Only overwrite if opaque
                if (spriteLineBuffer[screenX].x == 255 || slot == 0) {  // First in priority
                    spriteLineBuffer[screenX] = {
                        (uint8_t)screenX,
                        color,
                        palette,
                        behind,
                        s.isSprite0 && color != 0,
                        true
                    };
                }
            }
        }
    } break;
    }
}

void RendererLoopy::Serialize(Serializer& serializer) {
    RendererState state;
	state.m_scanline = m_scanline;
	state.v.coarse_x = loopy.v.coarse_x;
	state.v.coarse_y = loopy.v.coarse_y;
	state.v.nametable_x = loopy.v.nametable_x;
	state.v.nametable_y = loopy.v.nametable_y;
	state.v.fine_y = loopy.v.fine_y;

	state.t.coarse_x = loopy.t.coarse_x;
	state.t.coarse_y = loopy.t.coarse_y;
	state.t.nametable_x = loopy.t.nametable_x;
	state.t.nametable_y = loopy.t.nametable_y;
	state.t.fine_y = loopy.t.fine_y;

	state.x = loopy.x;
	state.w = loopy.w;
	state.pattern_lo_shift = m_shifts.pattern_lo_shift;
	state.pattern_hi_shift = m_shifts.pattern_hi_shift;
	state.attr_lo_shift = m_shifts.attr_lo_shift;
	state.attr_hi_shift = m_shifts.attr_hi_shift;
	state._frameCount = _frameCount;
	state.ppumask = ppumask;
    state.dot = dot;
    for (int i = 0; i < 8; ++i) {
        state.secondaryOAM[i].x = secondaryOAM[i].x;
        state.secondaryOAM[i].y = secondaryOAM[i].y;
        state.secondaryOAM[i].tileIndex = secondaryOAM[i].tileIndex;
        state.secondaryOAM[i].attributes = secondaryOAM[i].attributes;
        state.secondaryOAM[i].isSprite0 = secondaryOAM[i].isSprite0;
		state.spritePatternAddrHigh[i] = spritePatternAddrHigh[i];
		state.spritePatternAddrLow[i] = spritePatternAddrLow[i];
		state.spritePatternTableHigh[i] = spritePatternTableHigh[i];
		state.spritePatternTableLow[i] = spritePatternTableLow[i];
    }
	serializer.Write(state);
}

void RendererLoopy::Deserialize(Serializer& serializer) {
	RendererState state;
	serializer.Read(state);
	m_scanline = state.m_scanline;
	loopy.v.coarse_x = state.v.coarse_x;
	loopy.v.coarse_y = state.v.coarse_y;
	loopy.v.nametable_x = state.v.nametable_x;
	loopy.v.nametable_y = state.v.nametable_y;
	loopy.v.fine_y = state.v.fine_y;

	loopy.t.coarse_x = state.t.coarse_x;
	loopy.t.coarse_y = state.t.coarse_y;
	loopy.t.nametable_x = state.t.nametable_x;
	loopy.t.nametable_y = state.t.nametable_y;
	loopy.t.fine_y = state.t.fine_y;

	loopy.x = state.x;
	loopy.w = state.w;
	m_shifts.pattern_lo_shift = state.pattern_lo_shift;
	m_shifts.pattern_hi_shift = state.pattern_hi_shift;
	m_shifts.attr_lo_shift = state.attr_lo_shift;
	m_shifts.attr_hi_shift = state.attr_hi_shift;
	_frameCount = state._frameCount;
	ppumask = state.ppumask;
	dot = state.dot;
    for (int i = 0; i < 8; ++i) {
        secondaryOAM[i].x = state.secondaryOAM[i].x;
        secondaryOAM[i].y = state.secondaryOAM[i].y;
        secondaryOAM[i].tileIndex = state.secondaryOAM[i].tileIndex;
        secondaryOAM[i].attributes = state.secondaryOAM[i].attributes;
        secondaryOAM[i].isSprite0 = state.secondaryOAM[i].isSprite0;
		spritePatternAddrHigh[i] = state.spritePatternAddrHigh[i];
		spritePatternAddrLow[i] = state.spritePatternAddrLow[i];
		spritePatternTableHigh[i] = state.spritePatternTableHigh[i];
		spritePatternTableLow[i] = state.spritePatternTableLow[i];
    }
}