// Second-order IIR sections designed with the RBJ Audio EQ Cookbook formulas,
// which is also what Equalizer APO uses for PK / LSC / HSC with a Q value.
#pragma once

#include <array>
#include <cmath>

namespace gdm {

enum class FilterType {
    Peak,       // APO "PK"
    LowShelf,   // APO "LSC" (corner frequency + Q)
    HighShelf,  // APO "HSC" (corner frequency + Q)
    LowPass,    // APO "LP" / "LPQ"
    HighPass,   // APO "HP" / "HPQ"
    BandPass,   // APO "BP" (constant 0 dB peak gain)
    Notch,      // APO "NO"
    AllPass,    // APO "AP"
};

const char* filterTypeName(FilterType t);

struct FilterSpec {
    FilterType type = FilterType::Peak;
    double freqHz = 1000.0;
    double gainDb = 0.0;   // ignored by LP/HP/BP/NO/AP
    double q = 0.70710678118654752;
};

// Coefficients normalised so that a0 == 1.
struct BiquadCoeffs {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
};

BiquadCoeffs designBiquad(const FilterSpec& spec, double sampleRate);

// |H(e^jw)| in dB at `freqHz`.
double biquadMagnitudeDb(const BiquadCoeffs& c, double freqHz, double sampleRate);

// The two sections of a 4th-order Butterworth low-pass (24 dB/oct).
std::array<FilterSpec, 2> butterworthLowPass4(double cutoffHz);

// Transposed direct form II, double precision state (keeps low-frequency
// shelves such as 23 Hz at 48 kHz clean).
struct BiquadState {
    double z1 = 0.0, z2 = 0.0;

    inline double process(const BiquadCoeffs& c, double x) {
        const double y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return y;
    }

    // Avoid denormal slowdowns once the signal has decayed to silence.
    inline void flushDenormals() {
        if (std::fabs(z1) < 1e-30) z1 = 0.0;
        if (std::fabs(z2) < 1e-30) z2 = 0.0;
    }

    void reset() { z1 = z2 = 0.0; }
};

}  // namespace gdm
