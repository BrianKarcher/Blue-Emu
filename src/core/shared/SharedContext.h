#pragma once
#include <atomic>
#include <cstdint>
#include <condition_variable>
#include <mutex>
#include <array>
#include "CommandQueue.h"

#define WIDTH 256
#define HEIGHT 240

class DebuggerContext;

// Synchronization context shared between the Core and UI threads.
// Prevents multi-threading issues when accessing shared resources like video buffer and debugger state.
class SharedContext {
private:
    static constexpr int BUFFER_COUNT = 3;
    static constexpr int FRAME_SIZE = WIDTH * HEIGHT;

    // Fixed buffers (no reallocations ever)
    std::array<std::array<uint32_t, FRAME_SIZE>, BUFFER_COUNT> buffers;

    // Buffer indices
    std::atomic<int> back_index{ 0 };   // Core writes here
    std::atomic<int> ready_index{ -1 }; // Completed frame
    std::atomic<int> front_index{ 1 };  // UI reads here

    std::mutex cv_mutex;
    std::condition_variable cv_frame_ready;
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
        return buffers[back_index.load(std::memory_order_relaxed)].data();
    }

    // --- CORE calls this ---
    // Signals that the back buffer is full and ready to be shown.
    void SubmitFrame() {
        int back = back_index.load(std::memory_order_relaxed);

        // Publish this buffer as ready
        int previous_ready = ready_index.exchange(back, std::memory_order_acq_rel);

        // Pick a new back buffer that is NOT front or ready
        int front = front_index.load(std::memory_order_acquire);
        int next_back = -1;

        for (int i = 0; i < BUFFER_COUNT; i++) {
            if (i != front && i != previous_ready) {
                next_back = i;
                break;
            }
        }

        back_index.store(next_back, std::memory_order_release);

        cv_frame_ready.notify_one();
    }

    // --- UI thread ---
    const uint32_t* WaitForNewFrame(int timeout_ms) {
        std::unique_lock<std::mutex> lock(cv_mutex);

        bool received = cv_frame_ready.wait_for(
            lock,
            std::chrono::milliseconds(timeout_ms),
            [this] { return ready_index.load(std::memory_order_acquire) != -1; }
        );

        if (!received) {
            return nullptr;
        }

        // Consume the ready buffer
        int ready = ready_index.exchange(-1, std::memory_order_acq_rel);
        front_index.store(ready, std::memory_order_release);

        return buffers[ready].data();
    }

    const uint32_t* GetFrontBuffer() {
        int front = front_index.load(std::memory_order_acquire);
        return buffers[front].data();
    }

	CommandQueue command_queue;
	DebuggerContext* debugger_context;
};