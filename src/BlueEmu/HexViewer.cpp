#include "HexViewer.h"
#include "imgui.h"
#include <cstdint>
#include "SharedContext.h"
#include "Core.h"
#include "DebuggerContext.h"
#include "MapperBase.h"

HexViewer::HexViewer(Core* core, SharedContext& sharedCtx) : _sharedCtx(sharedCtx), _dbgCtx(sharedCtx.debugger_context) {
    _bus = core->emulator.GetBus();
    hexSources[CPU_SOURCE] = HexReadCPU;
	hexSources[PPU_SOURCE] = HexReadPPU;
}

uint8_t HexReadPPU(HexViewer hexViewer, uint16_t addr) {
    if (addr < 0x2000) {
        return hexViewer._dbgCtx->ppuState.chrMemory[addr];
    }
    else if (addr < 0x3F00) {
        // Reading from nametables and attribute tables
        uint16_t mirroredAddr = addr - 0x2000; // Mirror nametables every 4KB
        return hexViewer._dbgCtx->ppuState.nametables[mirroredAddr];
    }
    else if (addr < 0x4000) {
        // Reading from palette RAM (mirrored every 32 bytes)
        uint8_t paletteAddr = addr & 0x1F;
        if (paletteAddr >= 0x10 && (paletteAddr % 4 == 0)) {
            paletteAddr -= 0x10; // Mirror universal background color
        }
        return hexViewer._dbgCtx->ppuState.palette[paletteAddr];
    }
    return 0;
}

uint8_t HexReadCPU(HexViewer hexViewer, uint16_t addr) {
	return hexViewer._bus->peek(addr);
}

void HexViewer::DrawMemoryViewer(const char* title, bool *hexOpen) {
	if (!*hexOpen) return;
    ImGui::Begin(title, hexOpen);

    // 1. Dropdown for Hex Source
    const char* sources[] = { "CPU Bus", "PPU VRAM", "OAM", "Palette" };
    static int current_source = 0; // Local state or use pMain->hexView

    ImGui::SetNextItemWidth(150); // Keep it compact
    if (ImGui::Combo("Source", &current_source, sources, IM_ARRAYSIZE(sources))) {
        // Update the core's hexView index when changed
        sourceIdx = current_source;

        // Optional: Update 'size' based on source
        if (sourceIdx == CPU_SOURCE) size = 0x10000; // CPU range
		else if (sourceIdx == PPU_SOURCE) size = 0x4000; // PPU range
    }

    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Switch between different memory address spaces.");

    ImGui::Separator();

    // Set a default size for the viewer
    ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);

    // We use 1 column for the Address, 16 for Hex data, and 1 for ASCII
    static ImGuiTableFlags flags = ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuterH |
        ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY;

    if (ImGui::BeginTable("MemTable", 18, flags, ImVec2(0, ImGui::GetContentRegionAvail().y))) {
        // Setup columns
        ImGui::TableSetupColumn("Addr", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        for (int i = 0; i < 16; i++) ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 25.0f);
        ImGui::TableSetupColumn("ASCII", ImGuiTableColumnFlags_WidthStretch);

        ImGui::TableHeadersRow();

        // Use Clipper: each row displays 16 bytes
        ImGuiListClipper clipper;
        clipper.Begin((int)((size + 15) / 16));

        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
                ImGui::TableNextRow();

                // Column 0: Address
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%04X:", row * 16);

                // Columns 1-16: Hex Bytes
                for (int n = 0; n < 16; n++) {
                    ImGui::TableSetColumnIndex(n + 1);
                    size_t addr = row * 16 + n;
                    if (addr < size) {
                        uint8_t val = hexSources[sourceIdx](*this, addr);

                        // Highlight non-zero values or specific addresses (like Stack or Zero Page)
                        if (val == 0) ImGui::TextDisabled("00");
                        else ImGui::Text("%02X", val);
                    }
                }

                // Column 17: ASCII representation
                ImGui::TableSetColumnIndex(17);
                char ascii[17];
                for (int n = 0; n < 16; n++) {
                    size_t addr = row * 16 + n;
                    uint8_t c = (addr < size) ? hexSources[sourceIdx](*this, addr) : ' ';
                    ascii[n] = (c >= 32 && c <= 126) ? c : '.';
                }
                ascii[16] = '\0';
                ImGui::TextUnformatted(ascii);
            }
        }
        ImGui::EndTable();
    }
    ImGui::End();
}