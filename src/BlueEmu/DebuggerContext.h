#pragma once
#include <cstdint>
#include <mutex>
#include <array>
#include <string>
#include <Windows.h>

#define WM_USER_BREAKPOINT_HIT (WM_USER + 1)

class DebuggerContext {
public:
	uint8_t memory_snapshot[65536];

    // 64KB of metadata
	// Here we will define what each byte represents. For example,
	// we can mark code vs data vs unknown, etc.
	// The CPU will write to this as it executes instructions.
    uint8_t memory_metadata[65536];

#define META_UNKNOWN 0x00
#define META_CODE    0x01
#define META_DATA    0x03

	// Log that an instruction was fetched from this address
    void LogInstructionFetch(uint16_t addr) {
		memory_metadata[addr] = META_CODE;
    }

	uint8_t GetMetadata(uint16_t addr) {
        return memory_metadata[addr];
	}

	void UpdateSnapshot(const uint8_t* ram, const uint8_t* rom) {
		// Use memcpy to grab a point-in-time state
		// This is done once per frame at the end of the VBlank/Loop
		std::memcpy(memory_snapshot, ram, 2048);
		// ... copy other relevant banks ...
	}

	uint8_t SafeRead(uint16_t addr) {
		return memory_snapshot[addr]; // UI thread only reads this
	}

    struct CpuState {
        uint16_t pc;
        uint8_t  a, x, y;
        uint8_t  sp;
        uint8_t  p;
        uint64_t cycle;
    };

    struct NesPpuState {
        uint8_t ctrl;
        uint8_t mask;
		uint8_t status;
        uint16_t scanline;
        uint16_t dot;
		uint16_t bgPatternTableAddr;
        uint16_t spritePatternTableAddr;
        uint16_t scrollX;
        uint16_t scrollY;
		std::array<uint8_t, 0x2000> chrMemory; // 8KB CHR snapshot
        std::array<uint8_t, 0x1000> nametables;
		std::array<uint8_t, 0x100> oam; // Sprite memory snapshot
		std::array<uint8_t, 32> palette;
        uint8_t mirrorMode;
    };

    bool HasBreakpoint(uint16_t addr);
	void ToggleBreakpoint(uint16_t addr);

    std::mutex mutex;
    std::atomic<uint8_t> breakpoints[65536];
    std::atomic<bool> is_paused{ false };
    std::atomic<bool> hit_breakpoint{ false };
    std::atomic<bool> continue_requested{ false };
    // Did the UI request a single step?
    std::atomic<bool> step_requested{ false };

    std::atomic<uint16_t> step_over_target{ 0xFFFF };

    CpuState lastState{};
    NesPpuState ppuState{};
};