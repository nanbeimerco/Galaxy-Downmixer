// The downmix engine: N-channel interleaved float in, same layout out, with
// the stereo result written to FL/FR and every other channel zeroed so that
// AudioFlinger's own downmix passes it through untouched.
//
// Signal flow (per frame):
//   input meter -> mute -> [non-LFE: matrix -> dry delay]
//                          [LFE: Butterworth LP4 -> matrix]
//   -> mix meter -> master gain -> common PEQ -> L/R swap -> per-ear PEQ
//   -> output meter -> FL/FR
//
// Threading:
//   configure()/reset()  control thread, never concurrently with process()
//   setSettings()        control thread, may run concurrently with process()
//   process()            audio thread, real-time safe (no locks, no allocation)
//   readMeters()         any thread
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

#include "gdm/Biquad.h"
#include "gdm/Meter.h"
#include "gdm/Settings.h"

namespace gdm {

class Engine {
public:
    static constexpr double kMaxDelayMs = 50.0;

    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Returns false for unsupported layouts (no channels, or more than 32).
    bool configure(double sampleRate, uint32_t channelMask);

    void setSettings(const Settings& settings);
    Settings settings() const;

    // `in` and `out` hold `frames` interleaved frames of numChannels() floats
    // and may point to the same buffer.
    void process(const float* in, float* out, size_t frames);

    // Clears filter and delay history.
    void reset();

    MeterSnapshot readMeters();

    double sampleRate() const { return sampleRate_; }
    uint32_t channelMask() const { return channelMask_; }
    int numChannels() const { return numChannels_; }

    // Dry delay in samples that the current settings resolve to.
    int dryDelaySamples() const;

    // Optional tap for recording downmixed audio before EQ.
    void setPreEqWriter(class AudioWriter* writer);
    class AudioWriter* preEqWriter() const;

private:
    struct Prepared;

    Prepared* build(const Settings& s) const;
    void installPending();
    void snapRamps(const Prepared& p);

    double sampleRate_ = 48000.0;
    uint32_t channelMask_ = 0;
    int numChannels_ = 0;
    uint32_t channelBits_[kMaxChannelBits] = {};
    bool isLfe_[kMaxChannelBits] = {};
    int idxFL_ = -1;
    int idxFR_ = -1;

    // Control-thread state.
    mutable std::mutex controlMutex_;
    Settings settings_;

    // Hand-over of prepared coefficient sets to the audio thread.
    Prepared* active_ = nullptr;               // owned by the audio thread
    std::atomic<Prepared*> pending_{nullptr};  // control -> audio
    std::atomic<Prepared*> retired_{nullptr};  // audio -> control (for deletion)

    // Audio-thread state.
    std::vector<float> delayL_, delayR_;
    size_t delayPos_ = 0;
    BiquadState lfeState_[kMaxChannelBits][2];
    BiquadState commonState_[2][kMaxPeqFilters];
    BiquadState earState_[2][kMaxPeqFilters];
    int commonCount_ = 0;
    int earCount_[2] = {0, 0};
    float muteGain_[kMaxChannelBits] = {};
    float swap_ = 0.0f;

    // Meters.
    AtomicMeter inMeter_[kMaxChannelBits];
    AtomicMeter mixMeter_[2];
    AtomicMeter outMeter_[2];
    std::atomic<uint32_t> clipped_{0};
    std::atomic<uint32_t> meterFrames_{0};

    // Pre-EQ recording tap.
    std::atomic<class AudioWriter*> preEqWriter_{nullptr};
};

}  // namespace gdm
