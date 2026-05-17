#include "DebuggerContext.h"
#include <string>

bool DebuggerContext::HasBreakpoint(uint16_t addr) {
    return breakpoints[addr].load(std::memory_order_relaxed) != 0;
}

void DebuggerContext::ToggleBreakpoint(uint16_t addr) {
    breakpoints[addr].store(~breakpoints[addr].load(std::memory_order_relaxed), std::memory_order_relaxed);
}