#pragma once

#include <array>
#include <cstdint>

class Core;
class SharedContext;
class DebuggerContext;
class Bus;

// Memory sources
#define CPU_SOURCE 0
#define PPU_SOURCE 1

class HexViewer
{
public:
	HexViewer(Core* core, SharedContext& sharedCtx);
	void DrawMemoryViewer(const char* title, bool* hexOpen);
	DebuggerContext* _dbgCtx;
	Bus* _bus;
private:
	int sourceIdx = CPU_SOURCE;
	size_t size = 0x10000;
	SharedContext& _sharedCtx;
	std::array<uint8_t(*)(HexViewer hexViewer, uint16_t), 2> hexSources;
};

uint8_t HexReadPPU(HexViewer hexViewer, uint16_t addr);
uint8_t HexReadCPU(HexViewer hexViewer, uint16_t addr);