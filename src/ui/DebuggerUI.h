#pragma once
#include <Windows.h>
#include <commctrl.h>
#include <array>
#include <string>
#include <unordered_map>
#include <vector>

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

		// The op code info are organized as a struct of lists because it makes it more likely to be
	// added into L1/2 cache.
	std::string _opcodeNames[256] = {
		//  0     1		2     3     4     5     6     7     8	  9     A     B     C     D     E     F
			"BRK","ORA","DMP","DMP","DMP","ORA","ASL","DMP","PHP","ORA","ASL","DMP","DMP","ORA","ASL","DMP", // 0
			"BPL","ORA","DMP","DMP","DMP","ORA","ASL","DMP","CLC","ORA","DMP","DMP","DMP","ORA","ASL","DMP", // 1
			"JSR","AND","DMP","DMP","BIT","AND","ROL","DMP","PLP","AND","ROL","DMP","BIT","AND","ROL","DMP", // 2
			"BMI","AND","DMP","DMP","DMP","AND","ROL","DMP","SEC","AND","DMP","DMP","DMP","AND","ROL","DMP", // 3
			"RTI","EOR","DMP","DMP","DMP","EOR","LSR","DMP","PHA","EOR","LSR","DMP","JMP","EOR","LSR","DMP", // 4
			"BVC","EOR","DMP","DMP","DMP","EOR","LSR","DMP","CLI","EOR","DMP","DMP","DMP","EOR","LSR","DMP", // 5
			"RTS","ADC","DMP","DMP","DMP","ADC","ROR","DMP","PLA","ADC","ROR","DMP","JMP","ADC","ROR","DMP", // 6
			"BVS","ADC","DMP","DMP","DMP","ADC","ROR","DMP","SEI","ADC","DMP","DMP","DMP","ADC","ROR","DMP", // 7
			"DMP","STA","DMP","DMP","STY","STA","STX","DMP","DEY","DMP","TXA","DMP","STY","STA","STX","DMP", // 8
			"BCC","STA","DMP","DMP","STY","STA","STX","DMP","TYA","STA","TXS","DMP","DMP","STA","DMP","DMP", // 9
			"LDY","LDA","LDX","DMP","LDY","LDA","LDX","DMP","TAY","LDA","TAX","DMP","LDY","LDA","LDX","DMP", // A
			"BCS","LDA","DMP","DMP","LDY","LDA","LDX","DMP","CLV","LDA","TSX","DMP","LDY","LDA","LDX","DMP", // B
			"CPY","CMP","DMP","DMP","CPY","CMP","DEC","DMP","INY","CMP","DEX","DMP","CPY","CMP","DEC","DMP", // C
			"BNE","CMP","DMP","DMP","DMP","CMP","DEC","DMP","CLD","CMP","DMP","DMP","DMP","CMP","DEC","DMP", // D
			"CPX","SBC","DMP","DMP","CPX","SBC","INC","DMP","INX","SBC","NOP","DMP","CPX","SBC","INC","DMP", // E
			"BEQ","SBC","DMP","DMP","DMP","SBC","INC","DMP","SED","SBC","DMP","DMP","DMP","SBC","INC","DMP"  // F
	};

	uint8_t _opcodeBytes[256] = {
		// 0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F 
		   2, 2, 0, 0, 0, 2, 2, 0, 1, 2, 1, 0, 0, 3, 3, 0, // 0
		   2, 2, 0, 0, 0, 2, 2, 0, 1, 3, 0, 0, 0, 3, 3, 0, // 1
		   3, 2, 0, 0, 2, 2, 2, 0, 1, 2, 1, 0, 3, 3, 3, 0, // 2
		   2, 2, 0, 0, 0, 2, 2, 0, 1, 3, 0, 0, 0, 3, 3, 0, // 3
		   1, 2, 0, 0, 0, 2, 2, 0, 1, 2, 1, 0, 3, 3, 3, 0, // 4
		   2, 2, 0, 0, 0, 2, 2, 0, 1, 3, 0, 0, 0, 3, 3, 0, // 5
		   1, 2, 0, 0, 0, 2, 2, 0, 1, 2, 1, 0, 3, 3, 3, 0, // 6
		   2, 2, 0, 0, 0, 2, 2, 0, 1, 3, 0, 0, 0, 3, 3, 0, // 7
		   0, 2, 0, 0, 2, 2, 2, 0, 1, 0, 1, 0, 3, 3, 3, 0, // 8
		   2, 2, 0, 0, 2, 2, 2, 0, 1, 3, 1, 0, 0, 3, 0, 0, // 9
		   2, 2, 2, 0, 2, 2, 2, 0, 1, 2, 1, 0, 3, 3, 3, 0, // A
		   2, 2, 0, 0, 2, 2, 2, 0, 1, 3, 1, 0, 3, 3, 3, 0, // B
		   2, 2, 0, 0, 2, 2, 2, 0, 1, 2, 1, 0, 3, 3, 3, 0, // C
		   2, 2, 0, 0, 0, 2, 2, 0, 1, 3, 0, 0, 0, 3, 3, 0, // D
		   2, 2, 0, 0, 2, 2, 2, 0, 1, 2, 1, 0, 3, 3, 3, 0, // E
		   2, 2, 0, 0, 0, 2, 2, 0, 1, 3, 0, 0, 0, 3, 3, 0  // F
	};

	enum AddressingMode {
		ACC,
		IMP,
		IMM,
		ZP,
		ZPX,
		ZPY,
		ABS,
		ABSX,
		ABSY,
		IND,
		INDX,
		INDY,
		REL,
		NONE
	};

	AddressingMode instMode[256] = {
		// 0     1	    2     3      4      5     6    7     8     9       A     B      C      D     E     F
		   IMP,  INDX, NONE, NONE,  NONE,  ZP,   ZP,  NONE, IMP,  IMM,    ACC,  NONE,  NONE,  ABS,  ABS,  NONE, // 0
		   REL,  INDY, NONE, NONE,  NONE,  ZPX,  ZPX, NONE, IMP,  ABSY,   NONE, NONE,  NONE,  ABSX, ABSX, NONE, // 1
		   ABS,  INDX, NONE, NONE,  ZP,    ZP,   ZP,  NONE, IMP,  IMM,    ACC,  NONE,  ABS,   ABS,  ABS,  NONE, // 2
		   REL,  INDY, NONE, NONE,  NONE,  ZPX,  ZPX, NONE, IMP,  ABSY,   NONE, NONE,  NONE,  ABSX, ABSX, NONE, // 3
		   IMP,  INDX, NONE, NONE,  NONE,  ZP,   ZP,  NONE, IMP,  IMM,    ACC,  NONE,  ABS,   ABS,  ABS,  NONE, // 4
		   REL,  INDY, NONE, NONE,  NONE,  ZPX,  ZPX, NONE, IMP,  ABSY,   NONE, NONE,  NONE,  ABSX, ABSX, NONE, // 5
		   IMP,  INDX, NONE, NONE,  NONE,  ZP,   ZP,  NONE, IMP,  IMM,    ACC,  NONE,  IND,   ABS,  ABS,  NONE, // 6
		   REL,  INDY, NONE, NONE,  NONE,  ZPX,  ZPX, NONE, IMP,  ABSY,   NONE, NONE,  NONE,  ABSX, ABSX, NONE, // 7
		   NONE, INDX, NONE, NONE,  ZP,    ZP,   ZP,  NONE, IMP,  NONE,   IMP,  NONE,  ABS,   ABS,  ABS,  NONE, // 8
		   REL,  INDY, NONE, NONE,  ZPX,   ZPX,  ZPY, NONE, IMP,  ABSY,   IMP,  NONE,  NONE,  ABSX, NONE, NONE, // 9
		   IMM,  INDX, IMM,  NONE,  ZP,    ZP,   ZP,  NONE, IMP,  IMM,    IMP,  NONE,  ABS,   ABS,  ABS,  NONE, // A
		   REL,  INDY, NONE, NONE,  ZPX,   ZPX,  ZPY, NONE, IMP,  ABSY,   IMP,  NONE,  ABSX,  ABSX, ABSY, NONE, // B
		   IMM,  INDX, NONE, NONE,  ZP,    ZP,   ZP,  NONE, IMP,  IMM,    IMP,  NONE,  ABS,   ABS,  ABS,  NONE, // C
		   REL,  INDY, NONE, NONE,  NONE,  ZPX,  ZPX, NONE, IMP,  ABSY,   NONE, NONE,  NONE,  ABSX, ABSX, NONE, // D
		   IMM,  INDX, NONE, NONE,  ZP,    ZP,   ZP,  NONE, IMP,  IMM,    IMP,  NONE,  ABS,   ABS,  ABS,  NONE, // E
		   REL,  INDY, NONE, NONE,  NONE,  ZPX,  ZPX, NONE, IMP,  ABSY,   NONE, NONE,  NONE,  ABSX, ABSX, NONE, // F
	};

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