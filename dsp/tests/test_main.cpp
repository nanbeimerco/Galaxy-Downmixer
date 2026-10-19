// Unit tests for the DSP core. Self-contained (no test framework needed).

#include <cmath>
#include <cstdio>
#include <functional>
#include <random>
#include <string>
#include <vector>

#include "gdm/ApoParser.h"
#include "gdm/Biquad.h"
#include "gdm/ChannelMap.h"
#include "gdm/Engine.h"

#ifndef GDM_TEST_DATA_DIR
#define GDM_TEST_DATA_DIR "."
#endif

namespace {

int g_checks = 0;
int g_failures = 0;

struct TestCase {
    const char* name;
    std::function<void()> fn;
};
std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}
struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

#define TEST(name)                                   \
    void name();                                     \
    const Registrar name##_registrar(#name, name);   \
    void name()

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
        }                                                                        \
    } while (0)

#define CHECK_NEAR(actual, expected, tol)                                                    \
    do {                                                                                     \
        ++g_checks;                                                                          \
        const double a_ = (actual), e_ = (expected);                                         \
        if (!(std::fabs(a_ - e_) <= (tol))) {                                                \
            ++g_failures;                                                                    \
            std::printf("  FAIL %s:%d: %s = %.9g, expected %.9g (tol %g)\n", __FILE__,       \
                        __LINE__, #actual, a_, e_, static_cast<double>(tol));                \
        }                                                                                    \
    } while (0)

constexpr double kFs = 48000.0;
constexpr double kPi = 3.14159265358979323846;

using namespace gdm;

// Test fixture helpers -------------------------------------------------------

struct Rig {
    Engine engine;
    std::vector<uint32_t> bits;
    int n = 0;
    int iFL = -1, iFR = -1;

    explicit Rig(uint32_t mask, const Settings& s = Settings(), double fs = kFs) {
        engine.setSettings(s);
        engine.configure(fs, mask);
        bits = channelsInMask(mask);
        n = static_cast<int>(bits.size());
        for (int i = 0; i < n; ++i) {
            if (bits[i] == kFL) iFL = i;
            if (bits[i] == kFR) iFR = i;
        }
    }

    int index(uint32_t bit) const {
        for (int i = 0; i < n; ++i)
            if (bits[i] == bit) return i;
        return -1;
    }

    std::vector<float> run(const std::vector<float>& in, size_t block = 960) {
        std::vector<float> out(in.size());
        const size_t frames = in.size() / n;
        for (size_t pos = 0; pos < frames; pos += block) {
            const size_t len = std::min(block, frames - pos);
            engine.process(in.data() + pos * n, out.data() + pos * n, len);
        }
        return out;
    }

    std::vector<float> silence(size_t frames) const { return std::vector<float>(frames * n, 0.0f); }

    void sine(std::vector<float>& buf, uint32_t bit, double freq, double amp) const {
        const int ch = index(bit);
        const size_t frames = buf.size() / n;
        for (size_t f = 0; f < frames; ++f)
            buf[f * n + ch] += static_cast<float>(amp * std::sin(2.0 * kPi * freq * f / kFs));
    }

    float L(const std::vector<float>& out, size_t f) const { return out[f * n + iFL]; }
    float R(const std::vector<float>& out, size_t f) const { return out[f * n + iFR]; }
};

float peakOver(const std::vector<float>& out, const Rig& rig, int side, size_t from, size_t to) {
    float p = 0.0f;
    for (size_t f = from; f < to; ++f) {
        const float v = side == 0 ? rig.L(out, f) : rig.R(out, f);
        p = std::max(p, std::fabs(v));
    }
    return p;
}

FilterSpec pk(double f, double g, double q) { return {FilterType::Peak, f, g, q}; }

// Channel layout --------------------------------------------------------------

TEST(layout_7_1_4_order) {
    const auto bits = channelsInMask(kMask7_1_4);
    const uint32_t expected[] = {kFL, kFR, kFC, kLFE, kBL, kBR, kSL, kSR, kTFL, kTFR, kTBL, kTBR};
    CHECK(bits.size() == 12);
    for (size_t i = 0; i < 12 && i < bits.size(); ++i) CHECK(bits[i] == expected[i]);
    CHECK(std::string(channelName(kTBR)) == "TBR");
    CHECK(channelBitFromName("tfl") == kTFL);
    CHECK(defaultMaskForChannelCount(12) == kMask7_1_4);
}

TEST(default_matrix_values) {
    const DownmixMatrix m = DownmixMatrix::defaults();
    CHECK_NEAR(m.get(kFC).l, 0.70710678, 1e-7);
    CHECK_NEAR(m.get(kLFE).r, 2.26464431, 1e-6);
    CHECK(m.get(kTBL).l == 1.0f && m.get(kTBL).r == 0.0f);
    CHECK(m.get(kTSR).l == 0.0f && m.get(kTSR).r == 1.0f);
    CHECK(!m.isDefined(kFLC));  // not in the reference table -> contributes 0
}

// Biquads ---------------------------------------------------------------------

TEST(peak_gain_at_center) {
    const auto c = designBiquad(pk(1000, 6.0, 1.414), kFs);
    CHECK_NEAR(biquadMagnitudeDb(c, 1000, kFs), 6.0, 1e-9);
    CHECK_NEAR(biquadMagnitudeDb(c, 20, kFs), 0.0, 0.01);
    const auto d = designBiquad(pk(141, -6.31, 0.297), kFs);
    CHECK_NEAR(biquadMagnitudeDb(d, 141, kFs), -6.31, 1e-9);
}

TEST(shelf_corner_and_limits) {
    const auto ls = designBiquad({FilterType::LowShelf, 200, 6.0, 0.707}, kFs);
    CHECK_NEAR(biquadMagnitudeDb(ls, 1e-3, kFs), 6.0, 1e-6);
    CHECK_NEAR(biquadMagnitudeDb(ls, 200, kFs), 3.0, 1e-9);  // RBJ shelf: half gain at Fc
    CHECK_NEAR(biquadMagnitudeDb(ls, 23999, kFs), 0.0, 1e-3);
    const auto hs = designBiquad({FilterType::HighShelf, 5000, -4.0, 0.9}, kFs);
    CHECK_NEAR(biquadMagnitudeDb(hs, 1e-3, kFs), 0.0, 1e-6);
    CHECK_NEAR(biquadMagnitudeDb(hs, 5000, kFs), -2.0, 1e-9);
    CHECK_NEAR(biquadMagnitudeDb(hs, 23999, kFs), -4.0, 1e-3);
}

TEST(butterworth_lp4_125hz) {
    const auto spec = butterworthLowPass4(125.0);
    const auto a = designBiquad(spec[0], kFs);
    const auto b = designBiquad(spec[1], kFs);
    auto mag = [&](double f) { return biquadMagnitudeDb(a, f, kFs) + biquadMagnitudeDb(b, f, kFs); };
    CHECK_NEAR(mag(125.0), -3.0103, 1e-3);
    CHECK_NEAR(mag(1.0), 0.0, 1e-6);
    // Bilinear transform of an analogue Butterworth with pre-warping at fc.
    auto analog = [](double f) {
        const double wc = std::tan(kPi * 125.0 / kFs), w = std::tan(kPi * f / kFs);
        return -10.0 * std::log10(1.0 + std::pow(w / wc, 8.0));
    };
    for (double f : {50.0, 250.0, 500.0, 1000.0, 4000.0}) CHECK_NEAR(mag(f), analog(f), 1e-6);
    CHECK(mag(1000.0) < -70.0);  // ~24 dB/oct: three octaves above -> about -72 dB
}

// Engine: matrix, delay, LFE ------------------------------------------------------

TEST(matrix_impulse_per_channel) {
    const DownmixMatrix m = DownmixMatrix::defaults();
    for (uint32_t bit : channelsInMask(kMask7_1_4)) {
        if (bit == kLFE) continue;
        Rig rig(kMask7_1_4);
        auto in = rig.silence(400);
        in[rig.index(bit)] = 1.0f;
        const auto out = rig.run(in);
        CHECK(rig.engine.dryDelaySamples() == 172);
        bool othersZero = true, nonOutputZero = true;
        for (size_t f = 0; f < 400; ++f) {
            for (int ch = 0; ch < rig.n; ++ch) {
                if (ch != rig.iFL && ch != rig.iFR && out[f * rig.n + ch] != 0.0f) nonOutputZero = false;
            }
            if (f != 172 && (rig.L(out, f) != 0.0f || rig.R(out, f) != 0.0f)) othersZero = false;
        }
        CHECK_NEAR(rig.L(out, 172), m.get(bit).l, 1e-7);
        CHECK_NEAR(rig.R(out, 172), m.get(bit).r, 1e-7);
        CHECK(othersZero);
        CHECK(nonOutputZero);
    }
}

TEST(lfe_dc_gain_and_rejection) {
    Rig rig(kMask7_1_4);
    auto in = rig.silence(48000);
    for (size_t f = 0; f < 48000; ++f) in[f * rig.n + rig.index(kLFE)] = 0.25f;
    const auto out = rig.run(in);
    CHECK_NEAR(rig.L(out, 47999), 0.25 * 2.26464431, 1e-5);
    CHECK_NEAR(rig.R(out, 47999), 0.25 * 2.26464431, 1e-5);

    Rig rig2(kMask7_1_4);
    auto in2 = rig2.silence(48000);
    rig2.sine(in2, kLFE, 1000.0, 0.25);
    const auto out2 = rig2.run(in2);
    const float p = peakOver(out2, rig2, 0, 24000, 48000);
    CHECK(p < 0.25 * 2.26464431 * std::pow(10.0, -70.0 / 20.0));
}

TEST(lfe_bypass_when_lowpass_disabled) {
    Settings s;
    s.lfeLowPassEnabled = false;
    Rig rig(kMask7_1_4, s);
    auto in = rig.silence(400);
    in[rig.index(kLFE)] = 0.5f;
    const auto out = rig.run(in);
    // Without the low-pass the LFE is treated as a dry channel (delayed).
    CHECK_NEAR(rig.L(out, 172), 0.5 * 2.26464431, 1e-6);
    CHECK(rig.L(out, 0) == 0.0f);
}

TEST(delay_scales_with_sample_rate) {
    Rig rig(kMask7_1_4, Settings(), 44100.0);
    CHECK(rig.engine.dryDelaySamples() == 158);  // 3.58333 ms * 44.1
    Rig rig96(kMask7_1_4, Settings(), 96000.0);
    CHECK(rig96.engine.dryDelaySamples() == 344);
}

TEST(stereo_input_passes_through) {
    Rig rig(kMaskStereo);
    auto in = rig.silence(300);
    in[0] = 0.3f;      // FL
    in[2 * 5 + 1] = -0.4f;  // FR at frame 5
    const auto out = rig.run(in);
    CHECK_NEAR(rig.L(out, 172), 0.3, 1e-7);
    CHECK_NEAR(rig.R(out, 177), -0.4, 1e-7);
}

TEST(undefined_channel_contributes_nothing) {
    const uint32_t mask = kMaskStereo | kFLC;
    Rig rig(mask);
    auto in = rig.silence(300);
    in[rig.index(kFLC)] = 1.0f;
    const auto out = rig.run(in);
    CHECK(peakOver(out, rig, 0, 0, 300) == 0.0f);
    CHECK(peakOver(out, rig, 1, 0, 300) == 0.0f);
}

// Mute, swap, gains ----------------------------------------------------------------

TEST(mute_from_start_and_input_meter_still_reads) {
    Settings s;
    s.muteMask = kFC;
    Rig rig(kMask7_1_4, s);
    auto in = rig.silence(4800);
    rig.sine(in, kFC, 1000.0, 0.5);
    const auto out = rig.run(in);
    const MeterSnapshot m = rig.engine.readMeters();
    CHECK(peakOver(out, rig, 0, 0, 4800) == 0.0f);
    CHECK_NEAR(m.input[rig.index(kFC)].peak, 0.5, 1e-6);
    CHECK(m.mix[0].peak == 0.0f);
}

TEST(mute_mid_stream_ramps_to_silence) {
    Rig rig(kMask7_1_4);
    auto in = rig.silence(9600);
    rig.sine(in, kFL, 1000.0, 0.5);
    std::vector<float> out(in.size());
    rig.engine.process(in.data(), out.data(), 4800);
    Settings s;
    s.muteMask = kFL;
    rig.engine.setSettings(s);
    rig.engine.process(in.data() + 4800 * rig.n, out.data() + 4800 * rig.n, 4800);
    CHECK(peakOver(out, rig, 0, 2000, 4800) > 0.49f);
    // 10 ms ramp (480 samples) + 172 samples delay -> silent after ~5452.
    CHECK(peakOver(out, rig, 0, 4800 + 480 + 172, 9600) == 0.0f);
    // No instantaneous jump: the ramp spreads the change over 480 samples.
    float maxStep = 0.0f;
    for (size_t f = 4800 + 172; f < 4800 + 172 + 480; ++f)
        maxStep = std::max(maxStep, std::fabs(rig.L(out, f + 1) - rig.L(out, f)));
    CHECK(maxStep < 0.07f);  // a 1 kHz sine at 0.5 already moves ~0.065/sample
}

TEST(swap_moves_content) {
    Settings s;
    s.swapLR = true;
    Rig rig(kMask7_1_4, s);
    auto in = rig.silence(300);
    in[rig.index(kFL)] = 1.0f;
    const auto out = rig.run(in);
    CHECK_NEAR(rig.L(out, 172), 0.0, 1e-7);
    CHECK_NEAR(rig.R(out, 172), 1.0, 1e-7);
}

TEST(master_and_preamp_gains) {
    Settings s;
    s.masterGainDb = -6.0;
    s.commonPeq.preampDb = -3.0;
    s.leftPeq.preampDb = -1.0;
    Rig rig(kMask7_1_4, s);
    auto in = rig.silence(300);
    in[rig.index(kFL)] = 1.0f;
    in[rig.index(kFR)] = 1.0f;
    const auto out = rig.run(in);
    CHECK_NEAR(rig.L(out, 172), std::pow(10.0, -10.0 / 20.0), 1e-6);
    CHECK_NEAR(rig.R(out, 172), std::pow(10.0, -9.0 / 20.0), 1e-6);
}

// PEQ placement ---------------------------------------------------------------------

double steadyGainDb(const std::vector<float>& out, const Rig& rig, int side, double amp) {
    return 20.0 * std::log10(peakOver(out, rig, side, 24000, 48000) / amp);
}

TEST(common_peq_applies_to_both_sides) {
    Settings s;
    s.commonPeq.filters.push_back(pk(1000, 6.0, 1.0));
    Rig rig(kMask7_1_4, s);
    auto in = rig.silence(48000);
    rig.sine(in, kFL, 1000.0, 0.1);
    rig.sine(in, kFR, 1000.0, 0.1);
    const auto out = rig.run(in);
    CHECK_NEAR(steadyGainDb(out, rig, 0, 0.1), 6.0, 0.02);
    CHECK_NEAR(steadyGainDb(out, rig, 1, 0.1), 6.0, 0.02);
}

TEST(ear_peq_stays_on_physical_side_when_swapped) {
    Settings s;
    s.leftPeq.filters.push_back(pk(1000, 6.0, 1.0));  // fixes the left earpiece

    // No swap: FL content -> left earpiece -> boosted.
    {
        Rig rig(kMask7_1_4, s);
        auto in = rig.silence(48000);
        rig.sine(in, kFL, 1000.0, 0.1);
        const auto out = rig.run(in);
        CHECK_NEAR(steadyGainDb(out, rig, 0, 0.1), 6.0, 0.02);
        CHECK(peakOver(out, rig, 1, 0, 48000) == 0.0f);
    }
    // Swap: FL content -> right earpiece -> NOT boosted by the left correction.
    s.swapLR = true;
    {
        Rig rig(kMask7_1_4, s);
        auto in = rig.silence(48000);
        rig.sine(in, kFL, 1000.0, 0.1);
        const auto out = rig.run(in);
        CHECK_NEAR(steadyGainDb(out, rig, 1, 0.1), 0.0, 0.02);
        CHECK(peakOver(out, rig, 0, 0, 48000) == 0.0f);
    }
    // Swap: FR content -> left earpiece -> boosted.
    {
        Rig rig(kMask7_1_4, s);
        auto in = rig.silence(48000);
        rig.sine(in, kFR, 1000.0, 0.1);
        const auto out = rig.run(in);
        CHECK_NEAR(steadyGainDb(out, rig, 0, 0.1), 6.0, 0.02);
    }
}

TEST(peq_can_be_disabled) {
    Settings s;
    s.commonPeq.filters.push_back(pk(1000, 6.0, 1.0));
    s.leftPeq.preampDb = -20.0;
    s.commonPeqEnabled = false;
    s.earPeqEnabled = false;
    Rig rig(kMask7_1_4, s);
    auto in = rig.silence(300);
    in[rig.index(kFL)] = 1.0f;
    const auto out = rig.run(in);
    CHECK_NEAR(rig.L(out, 172), 1.0, 1e-7);
}

// Meters ------------------------------------------------------------------------------

TEST(meters_report_dbfs) {
    Rig rig(kMask7_1_4);
    auto in = rig.silence(4800);
    rig.sine(in, kFC, 1000.0, 0.5);
    rig.run(in);
    const MeterSnapshot m = rig.engine.readMeters();
    CHECK(m.frames == 4800);
    CHECK(m.numChannels == 12);
    CHECK(m.channelBits[2] == kFC);
    CHECK_NEAR(linearToDbfs(m.input[2].peak), -6.0206, 1e-3);
    CHECK_NEAR(m.input[2].rms, 0.5 / std::sqrt(2.0), 1e-4);
    CHECK_NEAR(m.mix[0].peak, 0.5 * 0.70710678, 1e-6);
    CHECK_NEAR(m.out[1].peak, 0.5 * 0.70710678, 1e-6);
    CHECK(m.clippedSamples == 0);
    CHECK(m.input[0].peak == 0.0f);

    const MeterSnapshot again = rig.engine.readMeters();  // reading clears
    CHECK(again.frames == 0 && again.input[2].peak == 0.0f);
}

TEST(clip_counter) {
    Rig rig(kMask7_1_4);
    auto in = rig.silence(1000);
    for (size_t f = 0; f < 1000; ++f) {
        in[f * rig.n + rig.index(kFL)] = 0.8f;
        in[f * rig.n + rig.index(kSL)] = 0.8f;  // sums to 1.6 on L
    }
    rig.run(in);
    const MeterSnapshot m = rig.engine.readMeters();
    CHECK(m.clippedSamples == 1000 - 172);
    CHECK_NEAR(linearToDbfs(m.out[0].peak), 20.0 * std::log10(1.6), 1e-4);
}

// Robustness ------------------------------------------------------------------------------

std::vector<float> noise(size_t count, unsigned seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> d(-0.3f, 0.3f);
    std::vector<float> v(count);
    for (auto& x : v) x = d(rng);
    return v;
}

Settings eqSettings() {
    Settings s;
    applyProfile(parseApoFile(std::string(GDM_TEST_DATA_DIR) + "/ig955_eq.txt"), s);
    s.muteMask = kTBL;
    return s;
}

TEST(in_place_matches_out_of_place) {
    const Settings s = eqSettings();
    Rig a(kMask7_1_4, s), b(kMask7_1_4, s);
    const auto in = noise(12 * 20000, 1);
    const auto outA = a.run(in);
    std::vector<float> buf = in;
    for (size_t pos = 0; pos < 20000; pos += 960) {
        const size_t len = std::min<size_t>(960, 20000 - pos);
        b.engine.process(buf.data() + pos * 12, buf.data() + pos * 12, len);
    }
    bool same = true;
    for (size_t i = 0; i < buf.size(); ++i) same = same && buf[i] == outA[i];
    CHECK(same);
}

TEST(block_size_does_not_change_result) {
    const Settings s = eqSettings();
    Rig a(kMask7_1_4, s), b(kMask7_1_4, s);
    const auto in = noise(12 * 20000, 2);
    const auto outA = a.run(in, 960);
    const auto outB = b.run(in, 37);
    double maxDiff = 0.0;
    for (size_t i = 0; i < in.size(); ++i) maxDiff = std::max(maxDiff, double(std::fabs(outA[i] - outB[i])));
    CHECK(maxDiff < 1e-7);
}

TEST(settings_update_while_running) {
    Rig rig(kMask7_1_4);
    const auto in = noise(12 * 960, 3);
    std::vector<float> out(in.size());
    for (int i = 0; i < 200; ++i) {
        Settings s = eqSettings();
        s.swapLR = (i % 2) == 0;
        s.masterGainDb = -0.01 * i;
        rig.engine.setSettings(s);
        if (i % 3 == 0) rig.engine.setSettings(s);  // overwrite a not-yet-consumed set
        rig.engine.process(in.data(), out.data(), 960);
    }
    CHECK(rig.engine.settings().masterGainDb < -1.98);
    bool finite = true;
    for (float v : out) finite = finite && std::isfinite(v);
    CHECK(finite);
}

// Equalizer APO parser -------------------------------------------------------------------

TEST(apo_parses_sample_file) {
    const ApoProfile p = parseApoFile(std::string(GDM_TEST_DATA_DIR) + "/ig955_eq.txt");
    CHECK(p.warnings.empty());
    for (const auto& w : p.warnings) std::printf("    warning: %s\n", w.c_str());
    CHECK_NEAR(p.common.preampDb, -1.5, 1e-12);
    CHECK(p.common.filters.size() == 7);
    CHECK_NEAR(p.left.preampDb, -1.7, 1e-12);
    CHECK(p.left.filters.size() == 10);
    CHECK(p.right.filters.size() == 10);
    if (p.common.filters.size() == 7) {
        const FilterSpec& f0 = p.common.filters[0];
        CHECK(f0.type == FilterType::LowShelf);
        CHECK_NEAR(f0.freqHz, 23, 1e-12);
        CHECK_NEAR(f0.gainDb, 1.37, 1e-12);
        CHECK_NEAR(f0.q, 0.758, 1e-12);
        CHECK(p.common.filters[6].type == FilterType::Peak);
        CHECK_NEAR(p.common.filters[6].freqHz, 4456, 1e-12);
    }
    if (p.left.filters.size() == 10 && p.right.filters.size() == 10) {
        CHECK(p.left.filters[1].type == FilterType::HighShelf);
        CHECK_NEAR(p.left.filters[2].gainDb, -1.74, 1e-12);
        CHECK_NEAR(p.right.filters[2].gainDb, 1.74, 1e-12);
        CHECK_NEAR(p.right.filters[9].q, 1.696, 1e-12);
    }
}

TEST(apo_edge_cases) {
    const std::string text =
        "\xEF\xBB\xBF# comment\r\n"
        "Preamp: -3 dB\r\n"
        "Preamp: -1dB\r\n"
        "Filter 1: OFF PK Fc 100 Hz Gain 3 dB Q 1\r\n"
        "Filter: ON PK Fc 1 kHz Gain 2 dB BW Oct 1\r\n"
        "Filter 3: ON LP Fc 18000 Hz\r\n"
        "Filter 4: ON LS Fc 100 Hz Gain 3 dB\r\n"
        "Filter 5: ON XYZ Fc 100 Hz\r\n"
        "Filter 6: ON PK Fc 100 Hz Gain 3 dB\r\n"
        "GraphicEQ: 20 0; 100 1\r\n"
        "Channel: C\r\n"
        "Filter 7: ON PK Fc 300 Hz Gain 3 dB Q 1\r\n"
        "Channel: 2\r\n"
        "Filter 8: ON HSC Fc 8000 Hz Gain -2 dB Q 0.7\r\n"
        "Channel: all\r\n"
        "Filter 9: ON NO Fc 60 Hz Q 30\r\n";
    const ApoProfile p = parseApo(text);
    CHECK_NEAR(p.common.preampDb, -4.0, 1e-12);
    CHECK(p.common.filters.size() == 3);  // PK (BW), LP, NO
    if (p.common.filters.size() == 3) {
        CHECK_NEAR(p.common.filters[0].freqHz, 1000.0, 1e-9);
        CHECK_NEAR(p.common.filters[0].q, 1.41421356, 1e-6);  // 1 octave
        CHECK(p.common.filters[1].type == FilterType::LowPass);
        CHECK_NEAR(p.common.filters[1].q, 0.70710678, 1e-6);
        CHECK(p.common.filters[2].type == FilterType::Notch);
    }
    CHECK(p.left.filters.empty());
    CHECK(p.right.filters.size() == 1);
    // LS, XYZ, PK without Q, GraphicEQ, Channel C
    CHECK(p.warnings.size() == 5);
    for (const auto& w : p.warnings) std::printf("    (expected) %s\n", w.c_str());
}

TEST(apo_utf16_file_text) {
    const std::string ascii = "Preamp: -2 dB\nFilter 1: ON PK Fc 500 Hz Gain 1 dB Q 2\n";
    std::string utf16 = "\xFF\xFE";
    for (char c : ascii) {
        utf16.push_back(c);
        utf16.push_back('\0');
    }
    const ApoProfile p = parseApo(utf16);
    CHECK_NEAR(p.common.preampDb, -2.0, 1e-12);
    CHECK(p.common.filters.size() == 1);
    CHECK(p.warnings.empty());
}

}  // namespace

int main() {
    for (const auto& t : registry()) {
        const int before = g_failures;
        t.fn();
        std::printf("%s %s\n", g_failures == before ? "[ OK ]" : "[FAIL]", t.name);
    }
    std::printf("\n%d checks, %d failures, %zu tests\n", g_checks, g_failures, registry().size());
    return g_failures == 0 ? 0 : 1;
}
