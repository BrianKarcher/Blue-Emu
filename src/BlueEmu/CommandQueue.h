#pragma once

#include <queue>
#include <string>
#include <mutex>

// =============================================================================
// Emulator Command Queue - For thread-safe commands
// =============================================================================
class CommandQueue {
public:
    enum class CommandType {
        LOAD_ROM,
        RESET,
        POWER,
        CLOSE,
        SAVE_STATE,
        LOAD_STATE,
        PAUSE,
        RESUME,
        STEP_FRAME,
        ADD_CONTROLLER,
        REMOVE_CONTROLLER
    };

    struct Command {
        CommandType type;
        std::string data; // For file paths, etc.
    };

    void Push(Command cmd) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push(cmd);
        m_cv.notify_one();
    }

    bool TryPop(Command& cmd) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_queue.empty()) {
            return false;
        }
        cmd = m_queue.front();
        m_queue.pop();
        return true;
    }

    void WaitForCommand(Command& cmd) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this] { return !m_queue.empty(); });
        cmd = m_queue.front();
        m_queue.pop();
    }

private:
    std::queue<Command> m_queue;
    std::mutex m_mutex;
    std::condition_variable m_cv;
};