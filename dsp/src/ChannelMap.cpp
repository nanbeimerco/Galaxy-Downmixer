#include "gdm/ChannelMap.h"

#include <cctype>

namespace gdm {

namespace {

struct NamedBit {
    uint32_t bit;
    const char* name;
};

constexpr NamedBit kNames[] = {
    {kFL, "FL"},   {kFR, "FR"},   {kFC, "FC"},     {kLFE, "LFE"},  {kBL, "BL"},
    {kBR, "BR"},   {kFLC, "FLC"}, {kFRC, "FRC"},   {kBC, "BC"},    {kSL, "SL"},
    {kSR, "SR"},   {kTC, "TC"},   {kTFL, "TFL"},   {kTFC, "TFC"},  {kTFR, "TFR"},
    {kTBL, "TBL"}, {kTBC, "TBC"}, {kTBR, "TBR"},   {kTSL, "TSL"},  {kTSR, "TSR"},
    {kBFL, "BFL"}, {kBFC, "BFC"}, {kBFR, "BFR"},   {kLFE2, "LFE2"}, {kFWL, "FWL"},
    {kFWR, "FWR"},
};

bool equalsIgnoreCase(const char* a, const char* b) {
    for (; *a && *b; ++a, ++b) {
        if (std::toupper(static_cast<unsigned char>(*a)) !=
            std::toupper(static_cast<unsigned char>(*b))) {
            return false;
        }
    }
    return *a == *b;
}

}  // namespace

const char* channelName(uint32_t bit) {
    for (const auto& n : kNames) {
        if (n.bit == bit) return n.name;
    }
    return "?";
}

uint32_t channelBitFromName(const char* name) {
    for (const auto& n : kNames) {
        if (equalsIgnoreCase(n.name, name)) return n.bit;
    }
    return 0;
}

int bitIndex(uint32_t bit) {
    if (bit == 0 || (bit & (bit - 1)) != 0) return -1;
    int i = 0;
    while ((bit >> i) != 1u) ++i;
    return i;
}

std::vector<uint32_t> channelsInMask(uint32_t mask) {
    std::vector<uint32_t> out;
    for (int i = 0; i < kMaxChannelBits; ++i) {
        const uint32_t bit = 1u << i;
        if (mask & bit) out.push_back(bit);
    }
    return out;
}

uint32_t defaultMaskForChannelCount(int channels) {
    switch (channels) {
        case 1: return kFL;
        case 2: return kMaskStereo;
        case 6: return kMask5_1;
        case 8: return kMask7_1;
        case 12: return kMask7_1_4;
        default: return 0;
    }
}

void DownmixMatrix::set(uint32_t bit, float l, float r) {
    const int i = bitIndex(bit);
    if (i < 0) return;
    coeff[i] = {l, r};
    defined[i] = true;
}

MixCoeff DownmixMatrix::get(uint32_t bit) const {
    const int i = bitIndex(bit);
    return i < 0 ? MixCoeff{} : coeff[i];
}

bool DownmixMatrix::isDefined(uint32_t bit) const {
    const int i = bitIndex(bit);
    return i >= 0 && defined[i];
}

DownmixMatrix DownmixMatrix::defaults() {
    DownmixMatrix m;
    m.set(kFL, 1.0f, 0.0f);
    m.set(kFR, 0.0f, 1.0f);
    m.set(kFC, 0.70710678f, 0.70710678f);
    m.set(kLFE, 2.26464431f, 2.26464431f);
    m.set(kBL, 1.0f, 0.0f);
    m.set(kBR, 0.0f, 1.0f);
    m.set(kSL, 1.0f, 0.0f);
    m.set(kSR, 0.0f, 1.0f);
    m.set(kTFL, 1.0f, 0.0f);
    m.set(kTFR, 0.0f, 1.0f);
    m.set(kTSL, 1.0f, 0.0f);
    m.set(kTSR, 0.0f, 1.0f);
    m.set(kTBL, 1.0f, 0.0f);
    m.set(kTBR, 0.0f, 1.0f);
    return m;
}

}  // namespace gdm
