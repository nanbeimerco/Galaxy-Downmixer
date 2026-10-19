// User-facing configuration of the downmixer. Plain data; the Engine turns it
// into sample-rate-specific coefficients.
#pragma once

#include <cstdint>
#include <vector>

#include "gdm/Biquad.h"
#include "gdm/ChannelMap.h"

namespace gdm {

constexpr int kMaxPeqFilters = 32;  // per section

struct PeqSection {
    double preampDb = 0.0;
    std::vector<FilterSpec> filters;  // applied in order, at most kMaxPeqFilters
};

struct Settings {
    DownmixMatrix matrix = DownmixMatrix::defaults();

    // Channel bits (ChannelBit) that are muted before the matrix.
    uint32_t muteMask = 0;

    // Applied to every non-LFE channel so it lines up with the LFE low-pass.
    // 172 samples at 48 kHz; rescaled for other sample rates.
    double dryDelayMs = 172.0 / 48.0;

    // LFE path: 4th-order Butterworth low-pass (24 dB/oct).
    bool lfeLowPassEnabled = true;
    double lfeCutoffHz = 125.0;

    // Gain after the matrix, before any PEQ. Useful for headroom.
    double masterGainDb = 0.0;

    // Applied identically to L and R, before the swap.
    bool commonPeqEnabled = true;
    PeqSection commonPeq;

    // Swaps the programme content between the outputs. Sits between the common
    // PEQ and the per-ear PEQ so that leftPeq always reaches the physical left
    // earpiece, whether or not the content is swapped.
    bool swapLR = false;

    // Per-earpiece correction, bound to the physical output channel.
    bool earPeqEnabled = true;
    PeqSection leftPeq;
    PeqSection rightPeq;

    // Ramp time for mute and swap changes (avoids clicks).
    double rampMs = 10.0;
};

}  // namespace gdm
