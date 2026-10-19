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
    kParamReloadFiles = 20, // set: any value; re-reads options.conf and the EQ file
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

}  // namespace gdm::protocol
