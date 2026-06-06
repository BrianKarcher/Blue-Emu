#include "DebuggerContext.h"
#include <string>

bool DebuggerContext::HasBreakpoint(uint16_t addr) {
    return (breakpoints[addr >> 6].load(std::memory_order_relaxed) >> (addr & 63)) & 1;
}

void DebuggerContext::ToggleBreakpoint(uint16_t addr) {
    breakpoints[addr >> 6].fetch_xor(1ULL << (addr & 63), std::memory_order_relaxed);
}

void DebuggerContext::SetBreakpoint(uint16_t addr) {
    breakpoints[addr >> 6].fetch_or(1ULL << (addr & 63), std::memory_order_relaxed);
}

void DebuggerContext::ClearBreakpoint(uint16_t addr) {
    breakpoints[addr >> 6].fetch_and(~(1ULL << (addr & 63)), std::memory_order_relaxed);
}