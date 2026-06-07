// NES NesPpu Loopy Registers Implementation
// Based on the loopy register behavior documented by loopy

#include <stdint.h>
#include <stdbool.h>
#include "NesPpu.h"
#include "A12Mapper.h"
#include "Core.h"
#include "NesBus.h"
#include "SharedContext.h"
#include "RendererLoopy.h"
#include "Serializer.h"

RendererLoopy::RendererLoopy(SharedContext& ctx) : context(ctx) {
    spriteLineBuffer.fill({ 255, 0, 0, false, false, false });  // 255 = no sprite
}

void RendererLoopy::initialize(NesPpu* ppu) {
    m_ppu = ppu;
    m_bus = ppu->bus;
}

void RendererLoopy::reset() {
    // Loopy scroll/address registers
    *(uint16_t*)&loopy.v = 0;
    *(uint16_t*)&loopy.t = 0;
    loopy.x = 0;
    loopy.w = false;

    // Dot/scanline position — PPU must start at the top of a new frame
    dot = 0;
    m_scanline = 0;
    _frameCount = 0;

    // Per-frame flags
    ppumask = 0;
    m_frameTick = false;
    m_frameComplete = false;
    hasOverflowBeenSet = false;
    hasSprite0HitBeenSet = false;
	_skipNextVBlankSet = false;

    // Background shift registers and tile fetch pipeline
    m_shifts = {};
    tile = {};

    // Sprite evaluation state machine
    spr_eval = {};
    memset(secondaryOAM, 0xFF, sizeof(secondaryOAM));
    spriteLineBuffer.fill({ 255, 0, 0, false, false, false });

    // Sprite pattern fetch buffers
    memset(spritePatternTableLow,  0, sizeof(spritePatternTableLow));
    memset(spritePatternTableHigh, 0, sizeof(spritePatternTableHigh));
    memset(spritePatternAddrLow,   0, sizeof(spritePatternAddrLow));
    memset(spritePatternAddrHigh,  0, sizeof(spritePatternAddrHigh));

    memset(context.GetBackBuffer(), 0x00, WIDTH * HEIGHT * sizeof(uint32_t));
}

// Write to NesPpuCTRL ($2000)
void RendererLoopy::setNesPpuCTRL(uint8_t value) {
    // t: ...GH.. ........ <- d: ......GH
    // G = bit 1, H = bit 0 (nametable select)
    loopy.t.nametable_x = (value >> 0) & 1;
    loopy.t.nametable_y = (value >> 1) & 1;
}

// Write to NesPpuSCROLL ($2005)
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

// Read from NesPpuSCROLL ($2005)
uint8_t RendererLoopy::getScrollY() {
    return ((loopy.t.coarse_y & 0x1F) << 3) | (loopy.t.fine_y & 0x07);
}

uint8_t RendererLoopy::getScrollX() {
    return ((loopy.t.coarse_x & 0x1F) << 3) | (loopy.x & 0x07);
}

// Write to NesPpuADDR ($2006)
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
		// MMC3 IRQ handling: clock IRQ counter on NesPpuADDR write
        // "Should decrement when A12 is toggled via NesPpuADDR"
        if (m_ppu->m_mapper) {
            m_ppu->m_mapper->ClockIRQCounter(*t_ptr);
        }
    }
}

uint16_t RendererLoopy::getNesPpuAddr() {
    uint16_t* v_ptr = (uint16_t*)&loopy.v;
	return (*v_ptr & 0b11111111111111); // The address is 14 bits
}

// Read from NesPpuSTATUS ($2002)
void RendererLoopy::ppuReadStatus() {
    // w:                   <- 0
    loopy.w = false;
    // "Reading the flag on the dot before it is set (scanling 241, dot 0) causes it to read as 0 and be cleared"
    if (m_scanline == 241 && dot == 0) {
        _skipNextVBlankSet = true;
    }
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

// Increment VRAM address (after NesPpuDATA read/write)
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
    // Bits 5, 6, 7 of NesPpuMASK: R, G, B (NTSC)
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
    if (bgEnabled() && (dot > 8 || (ppumask & NesPpuMASK_BACKGRONDLEFT) != 0)) {
        uint8_t pixel = get_pixel();
        bgPaletteIndex = m_ppu->paletteTable[pixel];
        bgOpaque = pixel != 0;
        if (ppumask & NesPpuMASK_GRAYSCALE) {
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
    if (spriteEnabled() && (dot > 8 || (ppumask & NesPpuMASK_SPRITELEFT) != 0)) {
        bool useSprite = false;

        const auto& spr = spriteLineBuffer[x];
        if (spr.valid) {  // There's a sprite pixel here
            uint8_t palIdx = spr.palette + 4;
            uint8_t sprIdx = m_ppu->paletteTable[0x10 + (spr.palette << 2) + spr.colorIndex];
            if (ppumask & NesPpuMASK_GRAYSCALE) {
                sprIdx &= 0x30; // Grayscale mode: only use bits 4 and 5 for color
            }
            uint32_t sprColor = m_nesPalette[sprIdx];

            if (spr.isZero && bgOpaque && !hasSprite0HitBeenSet && x < 255) {
                hasSprite0HitBeenSet = true;
                m_ppu->SetNesPpuStatus(0x40);
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
/// Simulates the clock cycle of a NesPpu (Picture Processing Unit) renderer, handling pixel rendering,
/// background tile fetching, sprite evaluation, and other NesPpu operations.
/// This function is called once per NesPpu clock cycle (5.3M/sec) and updates the internal state of the
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
            if (!_skipNextVBlankSet) {
                m_frameComplete = true;
                // Inform EmulatorCore of frame completion.
                m_frameTick = true;
                m_ppu->m_ppuStatus |= NesPpuSTATUS_VBLANK;
                if (m_ppu->m_ppuCtrl & NMI_ENABLE) {
                    m_bus->cpu.setNMI(true);
                }
            }
			_skipNextVBlankSet = false;
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
            if (visibleScanline) {
                process_sprite_evaluation();
                renderPixel(buffer);
            }
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

void RendererLoopy::process_sprite_evaluation() {
    // secondary OAM clear
    if (dot <= 64) {
        secondaryOAM[(dot - 1) / 2] = 0xFF; // Clear sprite slot
        return;
    }
    else if (dot == 65) {
        spr_eval.phase = SpriteEval::RANGE_CHECK;
        spr_eval.n = 0;
        spr_eval.m = 0;
        spr_eval.sec_oam_ptr = 0;
        spr_eval.sprites_found = 0;
        spr_eval.overflow_reads = 0;
        spr_eval.sprite_zero_next = false;
        // Note: oam_addr should already be 0 at this point (cleared
        // at dot 257 of the previous scanline during rendering).
        // A non-zero oam_addr shifts the base read address, causing
        // the evaluation to start mid-OAM.  We model this below.
    }

    const bool odd_cycle = (dot & 1) != 0;
	auto oam = m_ppu->oam;

    // ==============================================================
    //  IDLE — Step 4
    // ==============================================================
    // Evaluation is complete but the bus keeps toggling.
    //   Odd  cycles: read primary OAM at the frozen (n, m) address.
    //                Because n overflowed past 63, the address wraps:
    //                (n & 63) * 4 + m, which resolves to m (since
    //                n & 63 == 0).
    //   Even cycles: read (not write) secondary OAM at the frozen
    //                pointer.  The write-enable is suppressed.
    if (spr_eval.phase == SpriteEval::IDLE) {
        if (odd_cycle) {
            // Hardware reads OAM at the stale address.  n has wrapped
            // to 0 (6-bit counter overflow); m retains its last value.
            uint8_t addr = (m_ppu->oamAddr + ((spr_eval.n & 0x3F) * 4)
                + spr_eval.m) & 0xFF;
            spr_eval.read_latch = oam[addr];
        }
        // Even: silent read of secondary_oam[sec_oam_ptr & 0x1F].
        // No state change.
        return;
    }

    // ==============================================================
    //  ODD CYCLE — Read from primary OAM
    // ==============================================================
    if (odd_cycle) {
        // The hardware address bus outputs (oam_addr + n*4 + m) & $FF.
        // oam_addr is normally 0; non-zero values are a known source
        // of "OAM corruption" glitches.
        uint8_t addr = (m_ppu->oamAddr + spr_eval.n * 4 + spr_eval.m) & 0xFF;
        spr_eval.read_latch = oam[addr];
        return; // State machine only advances on even cycles.
    }

    // ==============================================================
    //  EVEN CYCLE — Write / evaluate / advance state
    // ==============================================================
    const int sprite_height = (m_ppu->m_ppuCtrl & 0x20) ? 16 : 8;

    switch (spr_eval.phase) {

        // ==============================================================
        //  Step 1: RANGE_CHECK — Is this sprite on the next scanline?
        // ==============================================================
    case SpriteEval::RANGE_CHECK: {
        // The hardware unconditionally writes the latch to secondary
        // OAM at the current write pointer, even before knowing
        // whether the sprite is in range.  If it's out of range the
        // pointer doesn't advance, so the byte will be overwritten
        // by the next sprite's Y.  This write is suppressed once
        // secondary OAM is full (sprites_found == 8), but by that
        // point we've already transitioned to OVERFLOW_EVAL.
        secondaryOAM[spr_eval.sec_oam_ptr] = spr_eval.read_latch;

        const int diff = m_scanline - spr_eval.read_latch;
        const bool in_range = (diff >= 0) && (diff < sprite_height);

        if (in_range) {
            // Mark sprite 0 presence for hit detection.
            if (spr_eval.n == 0)
                spr_eval.sprite_zero_next = true;

            // Y byte is already written above; advance pointer.
            spr_eval.sec_oam_ptr++;
            spr_eval.m = 1;
            spr_eval.phase = SpriteEval::COPY_BYTES;
        }
        else {
            // Out of range.  Y write will be overwritten.
            // Advance to next sprite; m stays 0.
            spr_eval.n++;
            if (spr_eval.n >= 64)
                spr_eval.phase = SpriteEval::IDLE;
            // else: remain in RANGE_CHECK for the next sprite.
        }
        break;
    }

    // ==============================================================
    //  Step 2: COPY_BYTES — Copy bytes 1, 2, 3 into secondary OAM
    // ==============================================================
    case SpriteEval::COPY_BYTES: {
        secondaryOAM[spr_eval.sec_oam_ptr] = spr_eval.read_latch;
        spr_eval.sec_oam_ptr++;
        spr_eval.m++;

        if (spr_eval.m == 4) {
            // All 4 bytes of this sprite are copied.
            spr_eval.sprites_found++;
            spr_eval.m = 0;
            spr_eval.n++;

            if (spr_eval.n >= 64) {
                // Finished scanning all 64 sprites.
                spr_eval.phase = SpriteEval::IDLE;
            }
            else if (spr_eval.sprites_found >= 8) {
                // Secondary OAM is full.  Switch to overflow
                // evaluation.  m is correctly 0 at this point;
                // the bug only manifests on repeated misses in
                // step 3b.
                spr_eval.phase = SpriteEval::OVERFLOW_EVAL;
            }
            else {
                // More room — check the next sprite.
                spr_eval.phase = SpriteEval::RANGE_CHECK;
            }
        }
        break;
    }

    // ==============================================================
    //  Step 3: OVERFLOW_EVAL — Buggy scan for the 9th+ sprite
    // ==============================================================
    //
    // Writes to secondary OAM are suppressed (the bus performs a
    // read of secondary OAM instead).  The value read from primary
    // OAM[n*4 + m] is compared against the scanline as though it
    // were a Y coordinate, regardless of what byte m actually
    // indexes.
    //
    // BUG:  When the comparison fails (not in range), the hardware
    //       increments BOTH n and m.  It should only increment n
    //       (and reset m to 0).  Because m drifts, subsequent
    //       sprites have their tile-index, attribute, or X bytes
    //       misinterpreted as Y coordinates.
    //
    case SpriteEval::OVERFLOW_EVAL: {
        const int diff = m_scanline - spr_eval.read_latch;
        const bool in_range = (diff >= 0) && (diff < sprite_height);

        if (in_range) {
            // ---- Step 3a: sprite overflow detected ----
            // Set the overflow flag in $2002.
            m_ppu->m_ppuStatus |= 0x20;

            // Advance m (correctly) and read the remaining 3 bytes
            // of this sprite before going idle.
            spr_eval.m = (spr_eval.m + 1) & 3;
            spr_eval.overflow_reads = 3;
            spr_eval.phase = SpriteEval::OVERFLOW_COPY;
        }
        else {
            // ---- Step 3b: not in range (BUGGY) ----
            // Hardware increments n AND m without carry between
            // them.  m wraps independently at 4.
            spr_eval.n++;
            spr_eval.m = (spr_eval.m + 1) & 3; // <<< THE BUG

            if (spr_eval.n >= 64)
                spr_eval.phase = SpriteEval::IDLE;
            // else: stay in OVERFLOW_EVAL, re-check with skewed m.
        }
        break;
    }

    // ==============================================================
    //  Step 3a (cont.): OVERFLOW_COPY — Finish reading the sprite
    //                   that triggered the overflow flag
    // ==============================================================
    //
    // After finding an in-range sprite during overflow evaluation,
    // the hardware reads the remaining 3 bytes of that sprite entry
    // (m increments correctly: 1 → 2 → 3 → 0).  No data is written
    // anywhere.  After all 4 bytes have been touched, evaluation
    // transitions to IDLE.
    //
    case SpriteEval::OVERFLOW_COPY: {
        spr_eval.m = (spr_eval.m + 1) & 3;
        spr_eval.overflow_reads--;

        if (spr_eval.overflow_reads == 0) {
            spr_eval.n++;
            spr_eval.phase = SpriteEval::IDLE;
        }
        break;
    }

    default:
        break;
    }
}

//void RendererLoopy::evaluateSprites(int screenY, uint8_t *newOam) {
    //// Evaluate sprites for this scanline
    //int spriteCount = 0;
    //secondaryOAMSprite0Index = -1;
    //for (int i = 0; i < 64; ++i) {
    //    int spriteY = m_ppu->oam[i * 4]; // Y position of the sprite
    //    if (spriteY > 0xF0) {
    //        continue; // Empty sprite slot
    //    }
    //    int spriteHeight = (m_ppu->m_ppuCtrl & NesPpuCTRL_SPRITESIZE) == 0 ? 8 : 16;
    //    if (screenY >= spriteY && screenY < (spriteY + spriteHeight)) {
    //        if (spriteCount < 8) {
    //            // Copy sprite data to new OAM
				//// Y, Tile Index, Attributes, X
    //            newOam[spriteCount * 4] = m_ppu->oam[i * 4];
    //            newOam[spriteCount * 4 + 1] = m_ppu->oam[i * 4 + 1];
    //            newOam[spriteCount * 4 + 2] = m_ppu->oam[i * 4 + 2];
    //            newOam[spriteCount * 4 + 3] = m_ppu->oam[i * 4 + 3];
				//if (i == 0) secondaryOAMSprite0Index = spriteCount; // Track sprite 0 index in secondary OAM
    //            spriteCount++;
    //        }
    //        else {
    //            // Sprite overflow - more than 8 sprites on this scanline
    //            if (!hasOverflowBeenSet) {
    //                // Set sprite overflow flag only once per frame
    //                hasOverflowBeenSet = true;
    //                m_ppu->m_ppuStatus |= NesPpuSTATUS_SPRITE_OVERFLOW;
    //            }
    //            break;
    //        }
    //    }
    //}
//}

// Converting into a state machine
void RendererLoopy::prepareSpriteLine(int y) {
    int spriteHeight = (m_ppu->m_ppuCtrl & NesPpuCTRL_SPRITESIZE) ? 16 : 8;

    // Determine which sprite slot (0-7) we are fetching
    // dot 257-264 = slot 0, 265-272 = slot 1, etc.
    int slot = (dot - 257) / 8;
    int step = (dot - 257) % 8;

    uint8_t *s = &secondaryOAM[slot * 4];

    if (s[0] <= 0xF0) {
        int i = 0;
    }

    switch (step) {
    case 0: // Garbage NT Read
    case 1: // Garbage NT Read
    case 2: // Garbage AT Read
    case 3: // Garbage AT Read
        // Hardware performs dummy reads here, usually to NT/AT 
        m_ppu->ReadVRAM(0x2000 | s[1]);
        break;

    case 4: { // Fetch Pattern Low Byte
        uint8_t tileId = s[1];
        uint16_t addrLow;
        int row = (y - s[0]);
        bool flipV = s[2] & 0x80;
        if (flipV) {
            row = (spriteHeight - 1) - row;
        }
        if (spriteHeight == 16) {
            // 8x16: LSB of tile index determines pattern table ($0000 or $1000)
            uint16_t bank = (tileId & 1) * 0x1000;
            tileId &= 0xFE;

            if (s[0] >= 0xF0) row = 0; // Force valid row for dummy reads

            if (row >= 8) { // Bottom half of large sprite
                row -= 8;
                tileId |= 1;
            }

            spritePatternAddrLow[slot] = bank + tileId * 16 + row;
        }
        else {
            // 8x8: NesPpuCTRL Bit 3 determines pattern table
            uint16_t bank = (m_ppu->m_ppuCtrl & 0x08) ? 0x1000 : 0x0000;

            if (s[0] >= 0xF0) row = 0;

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
        if (s[0] >= 0xF0) return;

        int spriteY = s[0];
        int relY = y - spriteY;
        //if (relY < 0 || relY >= (spriteHeight)) continue;

        bool flipV = s[2] & 0x80;
        bool flipH = s[2] & 0x40;
        uint8_t palette = s[2] & 0x03;
        bool behind = s[2] & 0x20;

        if (flipV) relY = (spriteHeight - 1) - relY;

        // We cache the next line of sprite pixels into a line buffer for quick access during pixel rendering
        for (int x = 0; x < 8; ++x) {
            int screenX = s[3] + x;
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
                        // Sprite 0 is always in slot 0 of secondary OAM (evaluation
                        // starts from n=0). sprite_zero_next is set by the evaluation
                        // state machine and reset at dot 65 of each scanline.
                        slot == 0 && spr_eval.sprite_zero_next,
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
    state.skipNextVBlankSet = _skipNextVBlankSet;
    memcpy(state.secondaryOAM, secondaryOAM, 0x20);
    for (int i = 0; i < 8; ++i) {
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
	_skipNextVBlankSet = state.skipNextVBlankSet;
    memcpy(secondaryOAM, state.secondaryOAM, 0x20);
    for (int i = 0; i < 8; ++i) {
		spritePatternAddrHigh[i] = state.spritePatternAddrHigh[i];
		spritePatternAddrLow[i] = state.spritePatternAddrLow[i];
		spritePatternTableHigh[i] = state.spritePatternTableHigh[i];
		spritePatternTableLow[i] = state.spritePatternTableLow[i];
    }
}