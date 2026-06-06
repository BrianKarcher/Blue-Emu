#pragma once
#include <Windows.h>
#include <commctrl.h>
#include <array>
#include <string>
#include <unordered_map>
#include <vector>
#include "NesDisassembler.h"

class NesBus;
class Core;
class DebuggerContext;
class CPU;
class SharedContext;
class ImGuiIO;

class DebuggerUI
{
public:
	DebuggerUI(HINSTANCE hInst, Core& core, ImGuiIO& io);
	void ComputeDisplayMap();
	void DrawScrollableDisassembler(bool* debuggerOpen);
	void OpenGoToAddressDialog();
	void GoTo();
	void GoTo(uint16_t addr);
private:
	bool showGoToAddressDialog = false;
	ImGuiIO& io;
	uint16_t contextMenuAddr = 0;
	uint8_t *log;


	std::vector<uint16_t> displayList;
	// addr to index in displayList
	std::unordered_map<int, int> displayMap;
	NesBus* _bus;
	HINSTANCE hInst;

	Core& _core;
	DebuggerContext* dbgCtx;
	SharedContext* sharedCtx;
	std::wstring StringToWstring(const std::string& str);
	std::string Disassemble(uint16_t address);
	void ScrollToAddress(uint16_t targetAddr);
	bool needsJump;
	uint16_t jumpToAddress;
};