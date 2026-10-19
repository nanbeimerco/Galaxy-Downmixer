// Level meters shared between the audio thread (writer) and a UI/control
// thread (reader). Lock-free: the audio thread folds each processed block
// into atomics, the reader takes and clears them.
#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>

#include "gdm/ChannelMap.h"

namespace gdm {

struct MeterReading {
    float peak = 0.0f;  // max |x| since the previous read (linear, 1.0 = 0 dBFS)
    float rms = 0.0f;   // RMS since the previous read (linear)
};

struct MeterSnapshot {
    int numChannels = 0;
    uint32_t channelBits[kMaxChannelBits] = {};
    MeterReading input[kMaxChannelBits];  // per input channel, before mute
    MeterReading mix[2];                  // L/R right after the matrix
    MeterReading out[2];                  // physical L/R after all processing
    uint32_t clippedSamples = 0;          // output samples with |x| > 1.0
    uint32_t frames = 0;                  // frames covered by this snapshot
};

inline float linearToDbfs(float v, float floorDb = -120.0f) {
    if (!(v > 0.0f)) return floorDb;
    const float db = 20.0f * std::log10(v);
    return db < floorDb ? floorDb : db;
}

class AtomicMeter {
public:
    void add(float blockPeak, float blockSumSq) {
        float cur = peak_.load(std::memory_order_relaxed);
        while (blockPeak > cur &&
               !peak_.compare_exchange_weak(cur, blockPeak, std::memory_order_relaxed)) {
        }
        float s = sumSq_.load(std::memory_order_relaxed);
        while (!sumSq_.compare_exchange_weak(s, s + blockSumSq, std::memory_order_relaxed)) {
        }
    }

    MeterReading take(uint32_t frames) {
        MeterReading r;
        r.peak = peak_.exchange(0.0f, std::memory_order_relaxed);
        const float s = sumSq_.exchange(0.0f, std::memory_order_relaxed);
        r.rms = frames > 0 ? std::sqrt(s / static_cast<float>(frames)) : 0.0f;
        return r;
    }

private:
    std::atomic<float> peak_{0.0f};
    std::atomic<float> sumSq_{0.0f};
};

}  // namespace gdm
