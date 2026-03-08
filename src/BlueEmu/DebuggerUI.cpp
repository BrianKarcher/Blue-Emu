#include "DebuggerUI.h"
#include <Windows.h>
#include <commctrl.h>
#include "Core.h"
#include <string>
#include "resource.h"
#include "DebuggerContext.h"
#include "Bus.h"
#include "SharedContext.h"
#include <sstream>
#include "imgui.h"

#pragma comment(lib, "comctl32.lib")

DebuggerUI::DebuggerUI(HINSTANCE hInst, Core& core, ImGuiIO& io) : hInst(hInst), _core(core), io(io) {
	_bus = _core.emulator.GetBus();
	log = (uint8_t*)malloc(0x10000); // 64KB log buffer
    dbgCtx = _core.context.debugger_context;
	sharedCtx = &_core.context;

}

std::wstring DebuggerUI::StringToWstring(const std::string& str) {
    if (str.empty()) return L"";
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), nullptr, 0);
    std::wstring wstr(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &wstr[0], size_needed);
    return wstr;
}

void DebuggerUI::ComputeDisplayMap() {
    displayList.clear();
    displayMap.clear();

    for (int i = 0; i < 0x10000; ++i) {
        if (dbgCtx->memory_metadata[i] == META_CODE) {
            displayList.push_back(i);
			displayMap[i] = displayList.size() - 1;
        }
	}
}

std::string DebuggerUI::Disassemble(uint16_t address) {
    uint8_t opcode = _bus->peek(address);
	std::stringstream ss;
	ss << _opcodeNames[opcode];
    switch (instMode[opcode]) {
    case IMM: {
        uint8_t value = _bus->peek(address + 1);
        ss << " #$" << std::uppercase << std::hex << (int)value;
    } break;
    case ZP: {
        uint8_t addr = _bus->peek(address + 1);
		ss << " $" << std::uppercase << std::hex << (int)addr;
	} break;
    case ZPX: {
        uint8_t addr = _bus->peek(address + 1);
		ss << " $" << std::uppercase << std::hex << (int)addr << ",X ($" << std::uppercase << std::hex << (int)dbgCtx->lastState.x << ")";
	} break;
    case ZPY: {
		uint8_t addr = _bus->peek(address + 1);
		ss << " $" << std::uppercase << std::hex << (int)addr << ",Y ($" << std::uppercase << std::hex << (int)dbgCtx->lastState.y << ")";
	} break;
	case ABS: {
		uint16_t addr = _bus->peek(address + 1) | (_bus->peek(address + 2) << 8);
		uint8_t value = _bus->peek(addr);
		ss << " $" << std::uppercase << std::hex << (int)addr << " = ($" << std::uppercase << std::hex << (int)value << ")";
	} break;
	case ABSX: {
		uint16_t addr = _bus->peek(address + 1) | (_bus->peek(address + 2) << 8);
		uint8_t value = _bus->peek(addr + dbgCtx->lastState.x);
		ss << " $" << std::uppercase << std::hex << (int)addr << ",X ($" << std::uppercase << std::hex << (int)dbgCtx->lastState.x << ")" << " = ($" << std::uppercase << std::hex << (int)value << ")";
	} break;
	case ABSY: {
		uint16_t addr = _bus->peek(address + 1) | (_bus->peek(address + 2) << 8);
		uint8_t value = _bus->peek(addr + dbgCtx->lastState.y);
		ss << " $" << std::uppercase << std::hex << (int)addr << ",Y ($" << std::uppercase << std::hex << (int)dbgCtx->lastState.y << ")" << " = ($" << std::uppercase << std::hex << (int)value << ")";
	} break;
	case IND: {
		uint16_t addr = _bus->peek(address + 1) | (_bus->peek(address + 2) << 8);
		uint16_t ptr = (_bus->peek((addr & 0xFF00) | ((addr + 1) & 0x00FF)) << 8);
		ss << " ($" << std::uppercase << std::hex << (int)addr << ")" << " = ($" << std::uppercase << std::hex << (int)ptr << ")";
	} break;
	case INDX: {
		uint8_t addr = _bus->peek(address + 1);
		uint8_t ptr = (uint8_t)(addr + dbgCtx->lastState.x);
		uint8_t ptrAddr = (_bus->peek((ptr & 0xFF00) | ((ptr + 1) & 0x00FF)) << 8);
		ss << " ($" << std::uppercase << std::hex << (int)addr << ",X $" << std::uppercase << std::hex << (int)dbgCtx->lastState.x << ") $" << std::uppercase << std::hex << (int)ptr << " = (" << (int)ptrAddr << ")";
	} break;
	case INDY: {
		uint8_t addr = _bus->peek(address + 1);
		uint8_t ptrAddr = (_bus->peek((addr & 0xFF00) | ((addr + 1) & 0x00FF)) << 8);
		ss << " ($" << std::uppercase << std::hex << (int)addr << "),Y( $" << std::uppercase << std::hex << (int)dbgCtx->lastState.y << ") = $" << std::uppercase << std::hex << (int)ptrAddr;
	} break;
	case REL: {
		int8_t offset = (int8_t)_bus->peek(address + 1);
		uint16_t target = address + 2 + offset;
		ss << " $" << std::uppercase << std::hex << target;
	} break;
    case ACC:
        ss << " A ($" << std::uppercase << std::hex << (int)dbgCtx->lastState.a << ")";
		break;
    }
    return ss.str();
}

void DebuggerUI::OpenGoToAddressDialog() {
    showGoToAddressDialog = true;
}

void DebuggerUI::GoTo() {
	GoTo(dbgCtx->lastState.pc);
}

void DebuggerUI::GoTo(uint16_t addr) {
    auto it = displayMap.find(addr); // Ensure address is valid

    if (it != displayMap.end()) {
        needsJump = true;
        jumpToAddress = it->first;
    }
}

void DebuggerUI::ScrollToAddress(uint16_t targetAddr) {
    auto it = displayMap.find(targetAddr);
	if (it == displayMap.end()) return; // Address not found

    float lineHeight = ImGui::GetTextLineHeightWithSpacing();

    // 1. Calculate the pixel position of the target address
    float targetScrollY = it->second * lineHeight;

    // 2. Get current scroll state
    float currentScrollY = ImGui::GetScrollY();
    float windowHeight = ImGui::GetWindowHeight();

    // 3. Check if target is off-screen (with a small padding/margin)
    float margin = lineHeight * 2.0f;
    bool isOffScreen = (targetScrollY < currentScrollY + margin) ||
        (targetScrollY > currentScrollY + windowHeight - margin);

    if (isOffScreen) {
        // Move scroll so the target is centered
        float centerScroll = targetScrollY - (windowHeight * 0.5f);

        // Clamp to 0 so we don't scroll into negative space
        if (centerScroll < 0) centerScroll = 0;

        ImGui::SetScrollY(centerScroll);
    }
}

void DebuggerUI::DrawScrollableDisassembler(bool* debuggerOpen) {
	if (!*debuggerOpen) return;
    ImGui::Begin("Disassembler", debuggerOpen);

    if (showGoToAddressDialog) {
        ImGui::OpenPopup("Go To Address");
        showGoToAddressDialog = false;
	}

    if (ImGui::BeginPopupModal("Go To Address", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        static char addrBuf[5] = ""; // 4 hex chars + null
        ImGui::Text("Enter Hex Address:");

        // Auto-focus the input box when the popup appears
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();

        if (ImGui::InputText("##addr", addrBuf, 5, ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue)) {
            // Convert hex string to integer
            uint16_t targetAddr = (uint16_t)std::strtol(addrBuf, nullptr, 16);

            // Set the jump target
            this->jumpToAddress = targetAddr;
            this->needsJump = true;

            ImGui::CloseCurrentPopup();
        }

        if (ImGui::Button("Cancel")) { ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }

    // Use a table for easy alignment
    static ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;

    // Set the height of the scrollable area (0 = use all available space)
    if (ImGui::BeginTable("DisasTable", 3, flags, ImVec2(0, 400))) {
        ImGui::TableSetupColumn("BP", ImGuiTableColumnFlags_WidthFixed, 30.0f);
        ImGui::TableSetupColumn("Addr", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Instruction", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        if (this->needsJump) {
            ScrollToAddress(this->jumpToAddress);
            // We clear the flag here because we manually moved the scrollbar
            this->needsJump = false;
        }
        uint16_t currentPC = dbgCtx->lastState.pc;
        ImGuiListClipper clipper;
        clipper.Begin(displayList.size());

        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                uint16_t addr = (uint16_t)displayList[i];
                ImGui::TableNextRow();

                // Check if this row is where the PC is
                if (addr == currentPC) {
                    // Set the background color for the entire row
                    // Format: (Alpha, Blue, Green, Red) in hex
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 0.0f, 0.6f)));
                }
                // --- COLUMN 0: Breakpoint Icon ---
                ImGui::TableSetColumnIndex(0);
                bool hasBreakpoint = dbgCtx->HasBreakpoint(addr);
                if (hasBreakpoint) {
                    ImGui::TextColored(ImVec4(1, 0, 0, 1), "  O"); // Red circle for BP
                }

                // --- COLUMN 1: Address ---
                ImGui::TableSetColumnIndex(1);
                char addrStr[8];
                sprintf_s(addrStr, "$%04X", addr);

                // Selectable makes the whole row clickable and easy to target for right-click
                if (ImGui::Selectable(addrStr, (dbgCtx->lastState.pc == addr), ImGuiSelectableFlags_SpanAllColumns)) {
                    // Jump logic or selecting logic here
                }

                // --- RIGHT CLICK CONTEXT MENU ---
                // This targets the Selectable we just created
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("Add Breakpoint", nullptr, hasBreakpoint)) {
                        if (hasBreakpoint) dbgCtx->breakpoints[addr].store(false);
                        else dbgCtx->breakpoints[addr].store(true);
                    }
                    if (ImGui::MenuItem("Copy Address")) {
                        ImGui::SetClipboardText(addrStr);
                    }
                    ImGui::EndPopup();
                }

                // --- COLUMN 2: Instruction ---
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(Disassemble(addr).c_str());
            }
        }
        ImGui::EndTable();
    }
    ImGui::End();
}