#pragma once

// Prevent Windows from defining min and max as macros
#define NOMINMAX
#include <atomic>
#include <vector>
#include <cstddef>
#include <stdexcept>
#include <algorithm>

template <typename T>
class AudioRingBuffer {
public:
    // Size MUST be a power of two for fast modulo arithmetic (used for wrapping)
    AudioRingBuffer(size_t capacity) {
        if (capacity == 0 || (capacity & (capacity - 1)) != 0) {
            throw std::invalid_argument("Capacity must be a power of two.");
        }
        m_capacity = capacity;
        m_buffer.resize(capacity);
    }

    // Producer (Emulator Core) - Lock-free
    size_t Write(const T* data, size_t count) {
        size_t available = GetAvailableWrite();
        size_t write_count = (count < available) ? count : available;

        if (write_count == 0) return 0;

        size_t write_idx = m_write.load(std::memory_order_relaxed);

        // Write in two chunks if wrapping is needed
        size_t first_chunk = (std::min)(write_count, m_capacity - write_idx);
        std::copy_n(data, first_chunk, m_buffer.data() + write_idx);

        if (write_count > first_chunk) {
            std::copy_n(data + first_chunk, write_count - first_chunk, m_buffer.data());
        }

        // Atomically update the write pointer
        m_write.store((write_idx + write_count) & (m_capacity - 1), std::memory_order_release);
        return write_count;
    }

    // Consumer (XAudio2 Backend) - Lock-free
    size_t GetAvailableRead() const {
        // Safe read: Ensures memory visibility of writes before reading `m_write`
        return (m_write.load(std::memory_order_acquire) - m_read.load(std::memory_order_relaxed)) & (m_capacity - 1);
    }

    // Consumer (XAudio2 Backend) - Get raw pointer and count for the next read
    // This allows zero-copy access for XAudio2 submission.
    size_t ReadPointer(const T** ptr) const {
        size_t read_idx = m_read.load(std::memory_order_relaxed);
        *ptr = m_buffer.data() + read_idx;

        // Return the number of contiguous samples from the read pointer until wrap or capacity
        return (std::min)(GetAvailableRead(), m_capacity - read_idx);
    }

    size_t Read(T* destination, size_t count) {
        size_t available = GetAvailableRead();
        size_t read_count = (std::min)(count, available);

        if (read_count == 0) return 0;

        size_t read_idx = m_read.load(std::memory_order_relaxed);

        // Read in two chunks if wrapping is needed
        size_t first_chunk = (std::min)(read_count, m_capacity - read_idx);
        std::copy_n(m_buffer.data() + read_idx, first_chunk, destination);

        if (read_count > first_chunk) {
            // Copy the "wrapped" part from the beginning of the ring buffer
            std::copy_n(m_buffer.data(), read_count - first_chunk, destination + first_chunk);
        }

        // Atomically update the read pointer
        m_read.store((read_idx + read_count) & (m_capacity - 1), std::memory_order_release);
        return read_count;
    }

    // Consumer (XAudio2 Backend) - Advance the read pointer
    void AdvanceRead(size_t count) {
        m_read.store((m_read.load(std::memory_order_relaxed) + count) & (m_capacity - 1), std::memory_order_release);
    }

    void Reset() {
        m_write.store(0, std::memory_order_relaxed);
		m_read.store(0, std::memory_order_relaxed);
    }

private:
    size_t GetAvailableWrite() const {
        return m_capacity - GetAvailableRead();
    }

    std::vector<T> m_buffer;
    size_t m_capacity;

    // SPSC pointers
    std::atomic<size_t> m_write{ 0 };
    std::atomic<size_t> m_read{ 0 };
};