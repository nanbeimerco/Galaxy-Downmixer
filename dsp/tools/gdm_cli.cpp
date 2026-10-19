// Offline runner: feeds a multichannel WAV through the Engine in HAL-sized
// blocks and writes the stereo result, printing the meter readings.
//
//   gdm_cli in.wav out.wav [--eq FILE] [--swap] [--mute FC,LFE,...]
//           [--master DB] [--mask 0x2D63F] [--block 960] [--no-lfe-lpf]
//           [--full] [--quiet]

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "Wav.h"
#include "gdm/ApoParser.h"
#include "gdm/Engine.h"

namespace {

void usage() {
    std::fprintf(stderr,
                 "usage: gdm_cli in.wav out.wav [--eq FILE] [--swap] [--mute FC,LFE,...]\n"
                 "               [--master DB] [--mask HEX] [--block N] [--no-lfe-lpf]\n"
                 "               [--full] [--quiet]\n"
                 "  --full   write all input channels (FL/FR carry the result, others 0)\n");
}

uint32_t parseMuteList(const std::string& list) {
    uint32_t mask = 0;
    size_t start = 0;
    while (start <= list.size()) {
        const size_t comma = list.find(',', start);
        const std::string name = list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        if (!name.empty()) {
            const uint32_t bit = gdm::channelBitFromName(name.c_str());
            if (bit == 0) {
                std::fprintf(stderr, "unknown channel name '%s'\n", name.c_str());
                std::exit(2);
            }
            mask |= bit;
        }
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return mask;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        usage();
        return 2;
    }
    const std::string inPath = argv[1], outPath = argv[2];
    gdm::Settings settings;
    uint32_t maskOverride = 0;
    size_t block = 960;
    bool full = false, quiet = false;
    std::string eqPath;

    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                usage();
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--eq") eqPath = next();
        else if (a == "--swap") settings.swapLR = true;
        else if (a == "--mute") settings.muteMask = parseMuteList(next());
        else if (a == "--master") settings.masterGainDb = std::atof(next().c_str());
        else if (a == "--mask") maskOverride = static_cast<uint32_t>(std::strtoul(next().c_str(), nullptr, 0));
        else if (a == "--block") block = static_cast<size_t>(std::strtoul(next().c_str(), nullptr, 0));
        else if (a == "--no-lfe-lpf") settings.lfeLowPassEnabled = false;
        else if (a == "--full") full = true;
        else if (a == "--quiet") quiet = true;
        else {
            usage();
            return 2;
        }
    }
    if (block == 0) block = 960;

    if (!eqPath.empty()) {
        const gdm::ApoProfile profile = gdm::parseApoFile(eqPath);
        for (const auto& w : profile.warnings) std::fprintf(stderr, "eq warning: %s\n", w.c_str());
        gdm::applyProfile(profile, settings);
        if (!quiet) {
            std::printf("EQ: common %zu filters (preamp %.2f dB), L %zu (%.2f dB), R %zu (%.2f dB)\n",
                        profile.common.filters.size(), profile.common.preampDb,
                        profile.left.filters.size(), profile.left.preampDb,
                        profile.right.filters.size(), profile.right.preampDb);
        }
    }

    wav::Audio in;
    std::string err;
    if (!wav::read(inPath, &in, &err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    uint32_t mask = maskOverride ? maskOverride : in.channelMask;
    if (mask == 0) mask = gdm::defaultMaskForChannelCount(in.channels);
    if (static_cast<int>(gdm::channelsInMask(mask).size()) != in.channels) {
        std::fprintf(stderr, "channel mask 0x%X does not match %d channels; use --mask\n",
                     mask, in.channels);
        return 1;
    }

    gdm::Engine engine;
    engine.setSettings(settings);
    if (!engine.configure(in.sampleRate, mask)) {
        std::fprintf(stderr, "unsupported layout\n");
        return 1;
    }

    const int n = in.channels;
    const size_t frames = in.frames();
    std::vector<float> outAll(in.samples.size());
    gdm::MeterSnapshot maxSnap;
    float maxIn[gdm::kMaxChannelBits] = {}, maxMix[2] = {}, maxOut[2] = {};
    uint32_t clipped = 0;

    for (size_t pos = 0; pos < frames; pos += block) {
        const size_t len = std::min(block, frames - pos);
        engine.process(in.samples.data() + pos * n, outAll.data() + pos * n, len);
        const gdm::MeterSnapshot s = engine.readMeters();
        for (int ch = 0; ch < n; ++ch) maxIn[ch] = std::max(maxIn[ch], s.input[ch].peak);
        for (int i = 0; i < 2; ++i) {
            maxMix[i] = std::max(maxMix[i], s.mix[i].peak);
            maxOut[i] = std::max(maxOut[i], s.out[i].peak);
        }
        clipped += s.clippedSamples;
        maxSnap = s;
    }

    wav::Audio out;
    out.sampleRate = in.sampleRate;
    if (full) {
        out.channels = n;
        out.channelMask = mask;
        out.samples = std::move(outAll);
    } else {
        const std::vector<uint32_t> bits = gdm::channelsInMask(mask);
        int iL = -1, iR = -1;
        for (int ch = 0; ch < n; ++ch) {
            if (bits[ch] == gdm::kFL) iL = ch;
            if (bits[ch] == gdm::kFR) iR = ch;
        }
        out.channels = 2;
        out.samples.resize(frames * 2);
        for (size_t f = 0; f < frames; ++f) {
            out.samples[2 * f] = iL >= 0 ? outAll[f * n + iL] : 0.0f;
            out.samples[2 * f + 1] = iR >= 0 ? outAll[f * n + iR] : 0.0f;
        }
    }
    if (!wav::writeFloat(outPath, out, &err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }

    if (!quiet) {
        std::printf("%s: %d ch, mask 0x%X, %d Hz, %zu frames, dry delay %d samples\n",
                    inPath.c_str(), n, mask, in.sampleRate, frames, engine.dryDelaySamples());
        std::printf("peak dBFS (input, before mute):\n");
        for (int ch = 0; ch < n; ++ch) {
            const bool muted = (settings.muteMask & maxSnap.channelBits[ch]) != 0;
            std::printf("  %-4s %8.2f%s\n", gdm::channelName(maxSnap.channelBits[ch]),
                        gdm::linearToDbfs(maxIn[ch]), muted ? "  (muted)" : "");
        }
        std::printf("mix  L %8.2f  R %8.2f\n", gdm::linearToDbfs(maxMix[0]), gdm::linearToDbfs(maxMix[1]));
        std::printf("out  L %8.2f  R %8.2f   clipped samples: %u\n", gdm::linearToDbfs(maxOut[0]),
                    gdm::linearToDbfs(maxOut[1]), clipped);
    }
    return 0;
}
