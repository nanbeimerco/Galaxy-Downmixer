#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "gdm/RingBuffer.h"

namespace gdm {

class AudioWriter {
public:
    explicit AudioWriter(size_t ringBufferCapacity = 65536);
    ~AudioWriter();

    // Starts recording to the given WAV file path.
    // Safe to call from control thread.
    bool start(const std::string& filePath, uint32_t sampleRate);

    // Stops recording and finalizes the WAV file.
    // Safe to call from control thread.
    void stop();

    // Push sample from audio processing thread.
    // Real-time safe: non-blocking, no allocation.
    void push(float l, float r);

    bool isRecording() const { return recording_.load(std::memory_order_acquire); }
    uint32_t sampleRate() const { return sampleRate_; }
    uint64_t recordedFrames() const { return recordedFrames_.load(std::memory_order_relaxed); }
    uint32_t droppedFrames() const { return droppedFrames_.load(std::memory_order_relaxed); }
    const std::string& currentFilePath() const { return filePath_; }

private:
    void threadLoop();
    void writeWavHeader(FILE* fp, uint32_t sampleRate, uint32_t dataBytes);

    SpscRingBuffer ringBuffer_;
    std::string filePath_;
    uint32_t sampleRate_ = 48000;

    std::atomic<bool> recording_{false};
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> recordedFrames_{0};
    std::atomic<uint32_t> droppedFrames_{0};

    std::thread workerThread_;
    FILE* file_ = nullptr;
};

}  // namespace gdm
