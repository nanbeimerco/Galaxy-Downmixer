// Channel layout helpers for AOSP audio_channel_mask_t output masks.
//
// Interleaved PCM in Android (and WAVE_FORMAT_EXTENSIBLE) orders channels by
// ascending bit position, so channel index i corresponds to the i-th set bit.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace gdm {

// Bit values from system/media/audio/include/system/audio-hal-enums.h.
// The first 18 bits are identical to the Microsoft WAV dwChannelMask bits.
enum ChannelBit : uint32_t {
    kFL   = 0x1,       // front left
    kFR   = 0x2,       // front right
    kFC   = 0x4,       // front center
    kLFE  = 0x8,       // low frequency
    kBL   = 0x10,      // back left
    kBR   = 0x20,      // back right
    kFLC  = 0x40,      // front left of center
    kFRC  = 0x80,      // front right of center
    kBC   = 0x100,     // back center
    kSL   = 0x200,     // side left
    kSR   = 0x400,     // side right
    kTC   = 0x800,     // top center
    kTFL  = 0x1000,    // top front left
    kTFC  = 0x2000,    // top front center
    kTFR  = 0x4000,    // top front right
    kTBL  = 0x8000,    // top back left
    kTBC  = 0x10000,   // top back center
    kTBR  = 0x20000,   // top back right
    kTSL  = 0x40000,   // top side left
    kTSR  = 0x80000,   // top side right
    kBFL  = 0x100000,  // bottom front left
    kBFC  = 0x200000,  // bottom front center
    kBFR  = 0x400000,  // bottom front right
    kLFE2 = 0x800000,  // low frequency 2
    kFWL  = 0x1000000, // front wide left
    kFWR  = 0x2000000, // front wide right
};

constexpr uint32_t kMaskStereo   = kFL | kFR;
constexpr uint32_t kMask5_1      = 0x3F;
constexpr uint32_t kMask7_1      = 0x63F;
constexpr uint32_t kMask7_1_4    = 0x2D63F;  // what Apple Music hands AudioFlinger

constexpr int kMaxChannelBits = 32;

// Short name such as "FL" or "TBR"; "?" for bits without a name.
const char* channelName(uint32_t bit);

// Inverse of channelName (case-insensitive). Returns 0 when unknown.
uint32_t channelBitFromName(const char* name);

inline bool isLfeBit(uint32_t bit) { return bit == kLFE || bit == kLFE2; }

int bitIndex(uint32_t bit);  // 0..31, or -1 if not a single bit

// Set bits of `mask` in interleave order.
std::vector<uint32_t> channelsInMask(uint32_t mask);

// A sensible mask for a WAV file that carries no explicit channel mask.
uint32_t defaultMaskForChannelCount(int channels);

struct MixCoeff {
    float l = 0.0f;
    float r = 0.0f;
};

// Downmix gains for every possible channel bit, indexed by bitIndex().
struct DownmixMatrix {
    std::array<MixCoeff, kMaxChannelBits> coeff{};
    std::array<bool, kMaxChannelBits> defined{};

    void set(uint32_t bit, float l, float r);
    MixCoeff get(uint32_t bit) const;
    bool isDefined(uint32_t bit) const;

    // The project's reference matrix (FC at -3 dB, LFE at +7.1 dB, every
    // left-side channel to L and right-side channel to R at unity).
    static DownmixMatrix defaults();
};

}  // namespace gdm
