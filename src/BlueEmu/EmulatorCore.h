// EmulatorCore.h
#pragma once
#include "Nes.h"
#include "AudioBackend.h"
#include "SharedContext.h"
#include <thread>

#ifdef _DEBUG
#define LOG(...) dbg(__VA_ARGS__)
#else
#define LOG(...) do {} while(0) // completely removed by compiler
#endif

//#define EMULATORCORE_DEBUG
#define FPS_CAP

class DebuggerContext;
class Bus;
class PPU;
class Cartridge;

class EmulatorCore
{
public:
	EmulatorCore(SharedContext& ctx);
	~EmulatorCore();

	void Initialize();
	void loadROM(const char* filepath);
	void reset();
	int runFrame();
	void start();
	void stop();
	Bus* GetBus() {
		return nes.bus_;
	}
	PPU* GetPPU() {
		return nes.ppu_;
	}
	Cartridge* GetCartridge() {
		return nes.cart_;
	}
	
private:
	inline void dbg(const wchar_t* fmt, ...);
	inline void processCommands();
	std::thread core_thread;
	void run();
	Nes nes;
	SharedContext& context;
	AudioBackend audioBackend;
	void processCommand(const CommandQueue::Command& cmd);
	bool m_paused;
	long long nextFrameUpdateTime;
	long long ticksPerFrame;
	long long freq;
	void UpdateNextFrameTime();
	void CreateSaveState();
	void LoadState();
	DebuggerContext* dbgCtx;
};