#include "PPUViewer.h"
#include "Bus.h"
#include "Core.h"
#include "EmulatorCore.h"
#include "Nes.h"
#include "imgui.h"
#include "DebuggerContext.h"

void PPUViewer::CreateTexture(GLuint& id, int width, int height) {
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

bool PPUViewer::Initialize(Core* core, SharedContext* sharedCtx) {
    _core = core;
    _cartridge = core->emulator.GetCartridge();
	_ppu = core->emulator.GetPPU();
    _sharedContext = sharedCtx;
    _dbgContext = _sharedContext->debugger_context;
    nt = new std::array<std::array<uint32_t, 256 * 240>, 4>();
	_oam = new std::array<uint32_t, 64 * 64>();
	_sprites = new std::array<uint32_t, 256 * 240>();
    for (int i = 0; i < 4; i++) {
        CreateTexture(ntTextures[i], 256, 240);
    }

    for (int i = 0; i < 4; i++) {
        CreateTexture(chr_textures[i], 128, 128);
    }

    CreateTexture(oam_texture, 64, 64);
    CreateTexture(sprite_texture, 256, 240);

    return true;
}

void PPUViewer::UpdateTexture(int index, std::array<uint32_t, 256 * 240>& data) {
    glBindTexture(GL_TEXTURE_2D, ntTextures[index]);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 240, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, data.data());
    glBindTexture(GL_TEXTURE_2D, 0); // Unbind to avoid state bleeding
}

void PPUViewer::DrawWrappedRect(ImDrawList* draw_list, ImVec2 canvas_p0, float x, float y) {
    float viewW = 256.0f;
    float viewH = 240.0f;
    float totalW = 512.0f;
    float totalH = 480.0f;

    // Normalize x/y to within the 512x480 space
    x = fmod(x, totalW);
    y = fmod(y, totalH);

    // Helper to draw segments
    auto draw_seg = [&](float sx, float sy, float sw, float sh) {
        if (sw <= 0 || sh <= 0) return;
        ImVec2 p1 = ImVec2(canvas_p0.x + sx, canvas_p0.y + sy);
        ImVec2 p2 = ImVec2(p1.x + sw, p1.y + sh);
        draw_list->AddRect(p1, p2, IM_COL32(0, 255, 0, 255), 0.0f, 0, 2.0f);
        draw_list->AddRectFilled(p1, p2, IM_COL32(0, 255, 0, 30));
    };

    // Split horizontally and vertically if crossing seams
    float w1 = std::min(viewW, totalW - x);
    float w2 = viewW - w1;
    float h1 = std::min(viewH, totalH - y);
    float h2 = viewH - h1;

    draw_seg(x, y, w1, h1);    // Top-Left piece
    draw_seg(0, y, w2, h1);    // Top-Right piece (wrapped)
    draw_seg(x, 0, w1, h2);    // Bottom-Left piece (wrapped)
    draw_seg(0, 0, w2, h2);    // Bottom-Right piece (wrapped)
}

uint16_t PPUViewer::GetBaseNametableAddress() {
    // Bits 0 and 1 of PPUCTRL select the nametable
    // 0 = $2000 (Top Left)
    // 1 = $2400 (Top Right)
    // 2 = $2800 (Bottom Left)
    // 3 = $2C00 (Bottom Right)
    uint8_t ctrl = _dbgContext->ppuState.ctrl;
    return (ctrl & 0x03);
}

// TODO Improve speed with dirty rectangles
// Also, cache the two nametables and only change them when needed (bank switch or write).
void PPUViewer::DrawNametables() {
    if (_core->isPlaying && _sharedContext->coreRunning.load(std::memory_order_relaxed)) {
		int nametableCount = _dbgContext->ppuState.mirrorMode == MapperBase::MirrorMode::FOUR_SCREEN ? 4 : 2;
        for (int i = 0; i < nametableCount; i++) {
            renderNametable((*nt)[i], i);
            UpdateTexture(i, (*nt)[i]);
        }

        // Logic to arrange the 4 slots based on mirroring
            // 0: Top-Left, 1: Top-Right, 2: Bottom-Left, 3: Bottom-Right
        int slots[4];
        if (_dbgContext->ppuState.mirrorMode == MapperBase::MirrorMode::HORIZONTAL) {
            slots[0] = 0; slots[1] = 0; slots[2] = 1; slots[3] = 1;
        }
        else if (_dbgContext->ppuState.mirrorMode == MapperBase::MirrorMode::FOUR_SCREEN) {
            slots[0] = 0; slots[1] = 1; slots[2] = 2; slots[3] = 3;
		}
        else {
            slots[0] = 0; slots[1] = 1; slots[2] = 0; slots[3] = 1;
        }

        ImVec2 canvas_p0 = ImGui::GetCursorScreenPos();
        ImGui::BeginGroup();
        // Set horizontal and vertical spacing between items to 0,0
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

        float w = 256, h = 240; // You can scale these

        // Top Row
        ImGui::Image((void*)(intptr_t)ntTextures[slots[0]], ImVec2(w, h)); ImGui::SameLine();
        ImGui::Image((void*)(intptr_t)ntTextures[slots[1]], ImVec2(w, h));

        // Bottom Row
        ImGui::Image((void*)(intptr_t)ntTextures[slots[2]], ImVec2(w, h)); ImGui::SameLine();
        ImGui::Image((void*)(intptr_t)ntTextures[slots[3]], ImVec2(w, h));

        ImGui::PopStyleVar(); // Always pop what you push
        ImGui::EndGroup();

        // Get the base nametable index (0-3) from PPUCTRL
        int ntIndex = GetBaseNametableAddress();

        // Get the fine scroll values from the Scroll Registers ($2005)
        int scrollX = _dbgContext->ppuState.scrollX;
        int scrollY = _dbgContext->ppuState.scrollY;

        // Map ntIndex to 512x480 coordinates
        // Index 0: (0,0), Index 1: (256, 0), Index 2: (0, 240), Index 3: (256, 240)
        float baseX = (ntIndex % 2) * 256.0f;
        float baseY = (ntIndex / 2) * 240.0f;

        // Final screen position of the scroll box
        float finalX = baseX + (float)scrollX;
        float finalY = baseY + (float)scrollY;

        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        // Use the Wrapped Logic to ensure the box appears correctly across boundaries
        DrawWrappedRect(draw_list, canvas_p0, finalX, finalY);

        ImGui::Text("PPUCTRL Bits: %d | Base Offset: (%.0f, %.0f)", ntIndex, baseX, baseY);
        ImGui::Text("Fine Scroll: (%d, %d)", scrollX, scrollY);
		std::string mirrorModeStr;
		switch (_dbgContext->ppuState.mirrorMode) {
		    case MapperBase::MirrorMode::HORIZONTAL:
			    mirrorModeStr = "Horizontal";
			    break;
		    case MapperBase::MirrorMode::VERTICAL:
			    mirrorModeStr = "Vertical";
			    break;
		    case MapperBase::MirrorMode::FOUR_SCREEN:
			    mirrorModeStr = "Four Screen";
			    break;
		}
        ImGui::Text("Mirror Mode: %s", mirrorModeStr.c_str());
    }
}

// This generates a 128x128 pixel buffer for one pattern table
void PPUViewer::UpdateCHRTexture(int bank, uint8_t palette_idx) {
    uint32_t pixels[128 * 128];

    std::array<uint32_t, 4> palette;
	_ppu->get_palette(palette_idx, palette);
    for (int tileY = 0; tileY < 16; tileY++) {
        for (int tileX = 0; tileX < 16; tileX++) {
            uint16_t offset = (bank * 0x1000) + (tileY * 16 + tileX) * 16;

            for (int row = 0; row < 8; row++) {
                uint8_t tile_l = _dbgContext->ppuState.chrMemory[offset + row];
                uint8_t tile_h = _dbgContext->ppuState.chrMemory[offset + row + 8];

                for (int col = 0; col < 8; col++) {
                    // Combine bits to get 0-3 color index
                    uint8_t pixel = ((tile_l >> (7 - col)) & 0x01) |
                        (((tile_h >> (7 - col)) & 0x01) << 1);

                    // Map color index to actual RGB (using a debug palette)
                    uint32_t color = palette[pixel];
                    pixels[(tileY * 8 + row) * 128 + (tileX * 8 + col)] = color | 0xFF000000;
                }
            }
        }
    }

    // Upload 'pixels' to your OpenGL texture for this bank
    glBindTexture(GL_TEXTURE_2D, chr_textures[bank]);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 128, 128, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, pixels);
}

void PPUViewer::DrawCHRViewer() {
    static int selected_palette = 0;

    // UI to select which palette to view the tiles with
    ImGui::SliderInt("Palette", &selected_palette, 0, 7);
    ImGui::Separator();

    ImGui::Columns(2, "CHRColumns", false);

    for (int i = 0; i < 2; i++) {
        UpdateCHRTexture(i, selected_palette);
        ImGui::Text("Pattern Table $%04X", i * 0x1000);

        // Display with 2x scaling (256x256)
        ImGui::Image((void*)(intptr_t)chr_textures[i], ImVec2(256, 256));
        ImGui::NextColumn();
    }
    ImGui::Columns(1);
}

void PPUViewer::Draw(const char* title, bool* p_open) {
	if (!*p_open) return;
    if (!ImGui::Begin(title, p_open)) {
        ImGui::End();
        return;
    }

    if (_core->isPlaying && _sharedContext->coreRunning.load(std::memory_order_relaxed)) {
        if (ImGui::BeginTabBar("PPUViews")) {

            if (ImGui::BeginTabItem("Nametables")) {
                DrawNametables();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("CHR Viewer")) {
                DrawCHRViewer();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("OAM")) {
                DrawOAMViewer();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Palette")) {
                // There are 8 palettes total: 4 for Background, 4 for Sprites
                // Each palette has 4 colors (but the first is always the shared backdrop)

                for (int i = 0; i < 8; i++) {
                    ImGui::Text(i < 4 ? "BG Palette %d" : "Sprite Palette %d", i % 4);
                    

                    for (int j = 0; j < 4; j++) {
                        ImGui::PushID((i * 4) + j); // Unique ID for each row of buttons
                        // Read the palette index from PPU VRAM ($3F00 range)
                        uint16_t addr = 0x3F00 + (i * 4) + j;
                        uint8_t colorIndex = _ppu->PeekVRAM(addr) & 0x3F; // Mask to 64 colors

                        // Convert NES color index to RGB (using your system palette array)
						uint32_t nesColor = m_nesPalette[colorIndex];
						uint8_t red = (nesColor >> 16) & 0xFF;
						uint8_t green = (nesColor >> 8) & 0xFF;
						uint8_t blue = nesColor & 0xFF;
                        ImVec4 color {(float)red / 0xFF, (float)green / 0xFF, (float)blue / 0xFF, (float)1.0f};

                        // 3. Draw a color square
                        ImGui::ColorButton("##color", color, ImGuiColorEditFlags_NoTooltip, ImVec2(40, 40));
                        ImGui::PopID();
                        if (j < 3) ImGui::SameLine();
                    }

                    
                    ImGui::Separator();
                }
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

void PPUViewer::renderNametable(std::array<uint32_t, 256 * 240>& buffer, int physicalTable)
{
    const uint16_t nametableAddr = physicalTable * 0x400;

    std::array<uint32_t, 4> palette;
    // Render the 32x30 tile nametable
    for (int row = 0; row < 30; row++) {
        for (int col = 0; col < 32; col++) {
            // Get the tile index from the nametable in VRAM
            uint8_t tileIndex = _dbgContext->ppuState.nametables[nametableAddr + row * 32 + col];
            if (tileIndex != 0xff) {
				int i = 0;
            }

            // Calculate pixel coordinates
            int pixelX = col * 8;  // Each tile is 8x8 pixels
            int pixelY = row * 8;

            int attrRow = row / 4;
            int attrCol = col / 4;
            // Get attribute byte for the tile
            uint16_t attrAddr = (nametableAddr | 0x3c0) + attrRow * 8 + attrCol;
            uint8_t attributeByte = _dbgContext->ppuState.nametables[attrAddr];
            if (attributeByte > 0) {
                int i = 0;
            }

            uint8_t paletteIndex = 0;
            get_palette_index_from_attribute(attributeByte, row, col, paletteIndex);
            _ppu->get_palette(paletteIndex, palette);
            // Render the tile at the calculated position
            render_tile(buffer, pixelY, pixelX, tileIndex, palette);
        }
    }
}

void PPUViewer::render_tile(std::array<uint32_t, 256 * 240>& buffer,
    int pr, int pc, int tileIndex, std::array<uint32_t, 4>& colors) {
    
    int tileOffset = tileIndex * 16; // 16 bytes per tile
    // Determine the pattern table base address
    uint16_t patternTableBase = _dbgContext->ppuState.bgPatternTableAddr;

    for (int y = 0; y < 8; y++) {
        
        int tileBase = patternTableBase + tileOffset; // 16 bytes per tile

        uint8_t byte1 = _dbgContext->ppuState.chrMemory[tileBase + y];     // bitplane 0
        uint8_t byte2 = _dbgContext->ppuState.chrMemory[tileBase + y + 8]; // bitplane 1

        for (int x = 0; x < 8; x++) {
            uint8_t bit0 = (byte1 >> (7 - x)) & 1;
            uint8_t bit1 = (byte2 >> (7 - x)) & 1;
            uint8_t colorIndex = (bit1 << 1) | bit0;

            uint32_t actualColor = 0;
            if (colorIndex == 0) {
                actualColor = m_nesPalette[_dbgContext->ppuState.palette[0]] | 0xFF000000; // Transparent color (background color)
            }
            else {
                actualColor = colors[colorIndex] | 0xFF000000; // Map to actual color from palette
            }
            int renderX = pc + x;
            int renderY = pr + y;
            if (renderX < 0 || renderY < 0 || renderX >= 256 || renderY >= 240)
                continue;
            buffer[(renderY * 256) + renderX] = actualColor;
        }
    }
}

void PPUViewer::get_palette_index_from_attribute(uint8_t attributeByte, int tileRow, int tileCol, uint8_t& paletteIndex)
{
    // Each attribute byte covers a 4x4 tile area (32x32 pixels)
    // Determine which quadrant of the attribute byte the tile is in
    int quadrantRow = (tileRow % 4) / 2; // 0 or 1
    int quadrantCol = (tileCol % 4) / 2; // 0 or 1
    // Extract the corresponding 2 bits for the palette index
    switch (quadrantRow) {
    case 0:
        switch (quadrantCol) {
        case 0:
            paletteIndex = attributeByte & 0x03; // Bits 0-1
            break;
        case 1:
            paletteIndex = (attributeByte >> 2) & 0x03; // Bits 2-3
            break;
        }
        break;
    case 1:
        switch (quadrantCol) {
        case 0:
            paletteIndex = (attributeByte >> 4) & 0x03; // Bits 4-5
            break;
        case 1:
            paletteIndex = (attributeByte >> 6) & 0x03; // Bits 6-7
            break;
        }
        break;
    }
}

void PPUViewer::DrawOAMViewer() {
    ImGui::Text("OAM (Object Attribute Memory) - 64 Sprites");
    ImGui::Separator();

    // Clear the OAM buffer
    _oam->fill(0xFF000000);
    int spriteHeight = (_dbgContext->ppuState.ctrl & PPUCTRL_SPRITESIZE) ? 16 : 8;
    if (spriteHeight == 16) {
		return; // For simplicity, we only handle 8x8 sprites in this viewer for now. 8x16 sprites require more complex handling.
    }

    // Render all 64 sprites to the OAM buffer (8x8 grid of 8x8 pixel tiles = 64x64)
    for (int spriteIdx = 0; spriteIdx < 64; spriteIdx++) {
        // OAM addresses: $0000-$00FF
        // Each sprite is 4 bytes: Y, Tile Index, Attributes, X
        uint16_t oamAddr = spriteIdx * 4;
        
        uint8_t yPos = _dbgContext->ppuState.oam[oamAddr];           // Y coordinate
        uint8_t tileIdx = _dbgContext->ppuState.oam[oamAddr + 1];   // Tile index
        uint8_t attributes = _dbgContext->ppuState.oam[oamAddr + 2]; // Attributes
        uint8_t xPos = _dbgContext->ppuState.oam[oamAddr + 3];       // X coordinate

        // Extract attributes
        uint8_t paletteIdx = (attributes & 0x03) + 4;      // Sprite palettes are 4-7
        bool flipH = (attributes & 0x40) != 0;
        bool flipV = (attributes & 0x80) != 0;
        bool priority = (attributes & 0x20) != 0;

        // Grid position (8x8 grid of sprites, each sprite rendered as 8x8 pixels)
        int gridX = (spriteIdx % 8) * 8;
        int gridY = (spriteIdx / 8) * 8;

        // Get sprite pattern table (depends on PPUCTRL bit 3)
        uint16_t spritePatternTableAddr = _dbgContext->ppuState.spritePatternTableAddr;
        uint16_t tileAddr = spritePatternTableAddr + (tileIdx * 16);

        // Render the tile to the OAM buffer
        std::array<uint32_t, 4> palette;
        _ppu->get_palette(paletteIdx, palette);

        for (int row = 0; row < 8; row++) {
            int actualRow = flipV ? (7 - row) : row;
            uint8_t byte1 = _dbgContext->ppuState.chrMemory[tileAddr + actualRow];
            uint8_t byte2 = _dbgContext->ppuState.chrMemory[tileAddr + actualRow + 8];

            for (int col = 0; col < 8; col++) {
                int actualCol = flipH ? (7 - col) : col;
                
                uint8_t bit0 = (byte1 >> (7 - actualCol)) & 1;
                uint8_t bit1 = (byte2 >> (7 - actualCol)) & 1;
                uint8_t colorIdx = (bit1 << 1) | bit0;

                uint32_t color = 0xFF000000; // Transparent by default
                if (colorIdx != 0) {
                    color = palette[colorIdx] | 0xFF000000;
                }

                int pixelX = gridX + col;
                int pixelY = gridY + row;
                
                if (pixelX < 64 && pixelY < 64) {
                    (*_oam)[pixelY * 64 + pixelX] = color;
                }
            }
        }
    }

    // Update the texture with the rendered OAM data
    glBindTexture(GL_TEXTURE_2D, oam_texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 64, 64, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, _oam->data());

    // Display the OAM texture (64x64 pixels, scaled up for visibility)
    ImGui::Image((void*)(intptr_t)oam_texture, ImVec2(512, 512));
    ImGui::Text("8x8 grid of sprite tiles from OAM");
}