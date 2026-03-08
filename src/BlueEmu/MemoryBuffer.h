#pragma once
#include <cstdint>
#include <vector>

/// <summary>
/// A memory buffer that simulates file-like read and seek operations.
/// </summary>
struct MemoryBuffer {
    const uint8_t* data;
    size_t size;
    size_t pos;

    MemoryBuffer(const std::vector<uint8_t>& vec) : data(vec.data()), size(vec.size()), pos(0) {}

    size_t read(void* dest, size_t bytes) {
        size_t available = size - pos;
        size_t toRead = (bytes < available) ? bytes : available;
        if (toRead > 0) {
            memcpy(dest, data + pos, toRead);
            pos += toRead;
        }
        return toRead;
    }

    void seek(long offset, int origin) {
        if (origin == SEEK_SET) pos = offset;
        else if (origin == SEEK_CUR) pos += offset;
        else if (origin == SEEK_END) pos = size + offset;
    }
};