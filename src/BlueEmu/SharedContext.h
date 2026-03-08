#pragma once
#include <mutex>
#include <condition_variable>
#include <cstdint>
#include <vector>
#include "CommandQueue.h"

#define WIDTH 256
#define HEIGHT 240

class DebuggerContext;

// Synchronization context shared between the Core and UI threads.
// Prevents multi-threading issues when accessing shared resources like video buffer and debugger state.
class SharedContext {
private:
    std::mutex video_mutex;
    std::condition_variable cv_frame_ready; // The signal object

    // We allocate the memory once.
    std::vector<uint32_t> buffer_1;
    std::vector<uint32_t> buffer_2;

    // Pointers that will swap targets
    uint32_t* p_front_buffer = nullptr; // UI Reads this
    uint32_t* p_back_buffer = nullptr;  // Core Writes this

    // Flag to prevent spurious wakeups
    bool has_new_frame = false;
public:
    struct CpuState {
        uint16_t pc;
        uint8_t  a, x, y;
        uint8_t  sp;
        uint8_t  status;
        uint64_t cycle;
    };

    // Lock-free atomic input (UI writes, Core reads)
    std::atomic<uint8_t> atomic_input{ 0 };
    std::atomic<bool> is_running{ true };
    std::atomic<uint16_t> current_fps{ 0 };
    std::atomic<uint8_t> mirrorMode;
    std::atomic<bool> coreRunning{ false };

    SharedContext();

    // --- CORE calls this ---
    // Returns a pointer to the memory where the Core should draw the NEXT frame.
    // We do not lock here to allow the core to write freely.
    uint32_t* GetBackBuffer() {
        return p_back_buffer;
    }

    // --- CORE calls this ---
    // Signals that the back buffer is full and ready to be shown.
    void SwapBuffers() {
        {
            std::lock_guard<std::mutex> lock(video_mutex);
            std::swap(p_front_buffer, p_back_buffer);
            has_new_frame = true;
        } // Mutex releases here
        // Wake up the UI thread immediately
        cv_frame_ready.notify_one();
    }

    // --- UI calls this ---
    // Returns nullptr if timeout occurs (no new frame)
    const uint32_t* WaitForNewFrame(int timeout_ms) {
        std::unique_lock<std::mutex> lock(video_mutex);

        // This puts the thread to SLEEP. It releases the mutex while sleeping.
        // It wakes up ONLY if:
        // 1. notify_one() is called AND has_new_frame is true
        // 2. timeout_ms passes
        bool received = cv_frame_ready.wait_for(lock, std::chrono::milliseconds(timeout_ms),
            [this] { return has_new_frame; });

        if (received) {
            has_new_frame = false; // Reset for next time
            return p_front_buffer;
        }

        return nullptr; // Timed out (Core is lagging or paused)
    }

    // --- UI calls this ---
    // Returns the most recent completed frame. 
    // We return a const pointer because UI should only read.
    const uint32_t* GetFrontBuffer() {
        std::lock_guard<std::mutex> lock(video_mutex);
        return p_front_buffer;
    }

	CommandQueue command_queue;
	DebuggerContext* debugger_context;
};