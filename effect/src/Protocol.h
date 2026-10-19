// Parameter protocol between the effect and a controller (adb tool now, the
// control app later). Every parameter key is a single int32. Values are
// little-endian PODs.
//
// Settings are global to the library: a SET_PARAM on any instance (including
// a controller's own instance on a dummy session) applies to every instance.
// Meters / status are reported for the most recently active instance.
#pragma once

#include <stdint.h>

namespace gdm::protocol {

constexpr int32_t kVersion = 1;

enum Param : int32_t {
    kParamVersion = 0,      // get: int32 protocol version
    kParamStatus = 1,       // get: Status
    kParamMeters = 2,       // get: Meters (reading clears the peak/RMS accumulators)

    kParamEnabled = 10,     // get/set: int32 0/1 (0 = untouched pass-through)
    kParamMuteMask = 11,    // get/set: uint32 channel bits
    kParamSwap = 12,        // get/set: int32 0/1
    kParamMasterGainDb = 13,  // get/set: float
    kParamLfeLowPass = 14,  // get/set: int32 0/1
    kParamCommonPeqEnabled = 15,  // get/set: int32 0/1
    kParamEarPeqEnabled = 16,     // get/set: int32 0/1

    kParamReloadFiles = 20, // set: any value; re-reads options.conf and the EQ file

    // Equalizer APO text upload, in chunks so every binder call stays small:
    // BEGIN (uint32 total bytes), CHUNK (raw bytes, <= kEqChunkMax) ..., COMMIT.
    kParamEqTextBegin = 21,
    kParamEqTextChunk = 22,
    kParamEqTextCommit = 23,  // parses the uploaded text and applies it
    kParamEqSummary = 24,     // get: EqSummary of the active EQ
    kParamEqWarnings = 25,    // get: parser warnings, '\n' separated, truncated to the reply size
    kParamEqClear = 26,       // set: removes every PEQ filter and preamp
};

constexpr uint32_t kEqChunkMax = 512;
constexpr uint32_t kEqTextMax = 64 * 1024;

struct EqSummary {
    int32_t commonFilters;
    int32_t leftFilters;
    int32_t rightFilters;
    int32_t warningCount;
    float commonPreampDb;
    float leftPreampDb;
    float rightPreampDb;
    uint32_t textBytes;  // size of the text the active EQ came from
};

struct Status {
    uint32_t sampleRate;
    uint32_t channelMask;
    uint32_t format;          // audio_format_t
    uint32_t enabled;         // effect enabled by the framework
    uint64_t framesProcessed;
    int32_t sessionId;
    int32_t instanceCount;
};

struct Meters {
    uint32_t numChannels;
    uint32_t channelBits[32];
    float inPeak[32];
    float inRms[32];
    float mixPeak[2];
    float mixRms[2];
    float outPeak[2];
    float outRms[2];
    uint32_t clippedSamples;
    uint32_t frames;
};

// The app parses these byte for byte (android/.../effect/Protocol.kt).
static_assert(sizeof(Status) == 32, "Status layout");
static_assert(sizeof(Meters) == 428, "Meters layout");
static_assert(sizeof(EqSummary) == 32, "EqSummary layout");

}  // namespace gdm::protocol
