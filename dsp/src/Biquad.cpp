#include "gdm/Biquad.h"

#include <algorithm>
#include <complex>

namespace gdm {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

const char* filterTypeName(FilterType t) {
    switch (t) {
        case FilterType::Peak: return "PK";
        case FilterType::LowShelf: return "LSC";
        case FilterType::HighShelf: return "HSC";
        case FilterType::LowPass: return "LP";
        case FilterType::HighPass: return "HP";
        case FilterType::BandPass: return "BP";
        case FilterType::Notch: return "NO";
        case FilterType::AllPass: return "AP";
    }
    return "?";
}

BiquadCoeffs designBiquad(const FilterSpec& spec, double sampleRate) {
    // Keep the design well-defined for out-of-range input.
    const double nyquist = sampleRate * 0.5;
    const double f = std::clamp(spec.freqHz, 1.0, nyquist * 0.9999);
    const double q = std::max(spec.q, 1e-3);

    const double w0 = 2.0 * kPi * f / sampleRate;
    const double cs = std::cos(w0);
    const double sn = std::sin(w0);
    const double alpha = sn / (2.0 * q);
    const double A = std::pow(10.0, spec.gainDb / 40.0);
    const double sqA2alpha = 2.0 * std::sqrt(A) * alpha;

    double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
    switch (spec.type) {
        case FilterType::Peak:
            b0 = 1.0 + alpha * A;
            b1 = -2.0 * cs;
            b2 = 1.0 - alpha * A;
            a0 = 1.0 + alpha / A;
            a1 = -2.0 * cs;
            a2 = 1.0 - alpha / A;
            break;
        case FilterType::LowShelf:
            b0 = A * ((A + 1.0) - (A - 1.0) * cs + sqA2alpha);
            b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cs);
            b2 = A * ((A + 1.0) - (A - 1.0) * cs - sqA2alpha);
            a0 = (A + 1.0) + (A - 1.0) * cs + sqA2alpha;
            a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cs);
            a2 = (A + 1.0) + (A - 1.0) * cs - sqA2alpha;
            break;
        case FilterType::HighShelf:
            b0 = A * ((A + 1.0) + (A - 1.0) * cs + sqA2alpha);
            b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cs);
            b2 = A * ((A + 1.0) + (A - 1.0) * cs - sqA2alpha);
            a0 = (A + 1.0) - (A - 1.0) * cs + sqA2alpha;
            a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cs);
            a2 = (A + 1.0) - (A - 1.0) * cs - sqA2alpha;
            break;
        case FilterType::LowPass:
            b0 = (1.0 - cs) * 0.5;
            b1 = 1.0 - cs;
            b2 = (1.0 - cs) * 0.5;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cs;
            a2 = 1.0 - alpha;
            break;
        case FilterType::HighPass:
            b0 = (1.0 + cs) * 0.5;
            b1 = -(1.0 + cs);
            b2 = (1.0 + cs) * 0.5;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cs;
            a2 = 1.0 - alpha;
            break;
        case FilterType::BandPass:
            b0 = alpha;
            b1 = 0.0;
            b2 = -alpha;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cs;
            a2 = 1.0 - alpha;
            break;
        case FilterType::Notch:
            b0 = 1.0;
            b1 = -2.0 * cs;
            b2 = 1.0;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cs;
            a2 = 1.0 - alpha;
            break;
        case FilterType::AllPass:
            b0 = 1.0 - alpha;
            b1 = -2.0 * cs;
            b2 = 1.0 + alpha;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cs;
            a2 = 1.0 - alpha;
            break;
    }

    BiquadCoeffs c;
    c.b0 = b0 / a0;
    c.b1 = b1 / a0;
    c.b2 = b2 / a0;
    c.a1 = a1 / a0;
    c.a2 = a2 / a0;
    return c;
}

double biquadMagnitudeDb(const BiquadCoeffs& c, double freqHz, double sampleRate) {
    const double w = 2.0 * kPi * freqHz / sampleRate;
    const std::complex<double> z1 = std::polar(1.0, -w);
    const std::complex<double> z2 = z1 * z1;
    const std::complex<double> num = c.b0 + c.b1 * z1 + c.b2 * z2;
    const std::complex<double> den = 1.0 + c.a1 * z1 + c.a2 * z2;
    return 20.0 * std::log10(std::abs(num / den));
}

std::array<FilterSpec, 2> butterworthLowPass4(double cutoffHz) {
    // Pole-pair Qs of an N=4 Butterworth: 1 / (2 cos(k*pi/8)), k = 1, 3.
    FilterSpec a{FilterType::LowPass, cutoffHz, 0.0, 1.0 / (2.0 * std::cos(kPi / 8.0))};
    FilterSpec b{FilterType::LowPass, cutoffHz, 0.0, 1.0 / (2.0 * std::cos(3.0 * kPi / 8.0))};
    return {a, b};
}

}  // namespace gdm
