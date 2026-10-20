#pragma once

#include <atomic>
#include <cstddef>
#include <vector>

namespace gdm {

struct StereoSample {
    float l;
    float r;
};

// Lock-free Single-Producer Single-Consumer (SPSC) ring buffer.
// Safe for real-time audio threads to push while a worker thread pops.
class SpscRingBuffer {
public:
    explicit SpscRingBuffer(size_t capacityPowerOfTwo = 65536)
        : mask_(capacityPowerOfTwo - 1),
          buffer_(capacityPowerOfTwo) {
        // Ensure capacity is a power of two
        // (capacityPowerOfTwo & (capacityPowerOfTwo - 1)) == 0
    }

    // Called from Producer (Audio Thread). Never blocks, no allocations.
    // Returns true on success, false if buffer is full (sample dropped).
    bool push(float l, float r) {
        const size_t write = writePos_.load(std::memory_order_relaxed);
        const size_t read = readPos_.load(std::memory_order_acquire);

        if ((write - read) > mask_) {
            // Buffer full!
            return false;
        }

        buffer_[write & mask_] = {l, r};
        writePos_.store(write + 1, std::memory_order_release);
        return true;
    }

    // Push multiple samples from audio thread.
    // Returns number of frames actually written.
    size_t pushInterleaved(const float* stereo, size_t frames) {
        const size_t write = writePos_.load(std::memory_order_relaxed);
        const size_t read = readPos_.load(std::memory_order_acquire);
        const size_t available = (mask_ + 1) - (write - read);
        const size_t count = (frames < available) ? frames : available;

        for (size_t i = 0; i < count; ++i) {
            buffer_[(write + i) & mask_] = {stereo[i * 2 + 0], stereo[i * 2 + 1]};
        }
        writePos_.store(write + count, std::memory_order_release);
        return count;
    }

    // Called from Consumer (Writer Thread).
    // Returns number of samples read into `out` (up to `maxFrames`).
    size_t pop(StereoSample* out, size_t maxFrames) {
        const size_t write = writePos_.load(std::memory_order_acquire);
        const size_t read = readPos_.load(std::memory_order_relaxed);

        const size_t available = write - read;
        const size_t count = (maxFrames < available) ? maxFrames : available;

        for (size_t i = 0; i < count; ++i) {
            out[i] = buffer_[(read + i) & mask_];
        }
        readPos_.store(read + count, std::memory_order_release);
        return count;
    }

    size_t availableRead() const {
        const size_t write = writePos_.load(std::memory_order_relaxed);
        const size_t read = readPos_.load(std::memory_order_relaxed);
        return write - read;
    }

    void reset() {
        writePos_.store(0, std::memory_order_relaxed);
        readPos_.store(0, std::memory_order_relaxed);
    }

private:
    const size_t mask_;
    std::vector<StereoSample> buffer_;

    // Separate cache lines to prevent false sharing
    alignas(64) std::atomic<size_t> writePos_{0};
    alignas(64) std::atomic<size_t> readPos_{0};
};

}  // namespace gdm
