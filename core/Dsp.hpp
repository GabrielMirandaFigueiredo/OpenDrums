// OpenDrums core - small DSP helpers (no dependencies).
#pragma once

#include <algorithm>
#include <cmath>

namespace od {

inline float dbToGain(float db) { return std::pow(10.0f, db * 0.05f); }
inline float gainToDb(float g) { return 20.0f * std::log10(std::max(g, 1.0e-9f)); }

struct BiquadCoefs {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
};

// Stereo biquad, transposed direct form II.
struct Biquad {
    BiquadCoefs c;
    float z1[2] = {0, 0};
    float z2[2] = {0, 0};

    float process(int ch, float x) {
        const float y = c.b0 * x + z1[ch];
        z1[ch] = c.b1 * x - c.a1 * y + z2[ch];
        z2[ch] = c.b2 * x - c.a2 * y;
        return y;
    }
    void reset() { z1[0] = z1[1] = z2[0] = z2[1] = 0.0f; }
};

// RBJ audio-EQ-cookbook designs.
inline BiquadCoefs designPeaking(double sr, double freq, double gainDb, double q) {
    freq = std::clamp(freq, 10.0, sr * 0.45);
    q = std::clamp(q, 0.1, 20.0);
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * 3.14159265358979 * freq / sr;
    const double cs = std::cos(w0), alpha = std::sin(w0) / (2.0 * q);
    const double a0 = 1.0 + alpha / A;
    BiquadCoefs c;
    c.b0 = static_cast<float>((1.0 + alpha * A) / a0);
    c.b1 = static_cast<float>((-2.0 * cs) / a0);
    c.b2 = static_cast<float>((1.0 - alpha * A) / a0);
    c.a1 = static_cast<float>((-2.0 * cs) / a0);
    c.a2 = static_cast<float>((1.0 - alpha / A) / a0);
    return c;
}

inline BiquadCoefs designLowShelf(double sr, double freq, double gainDb, double slope = 1.0) {
    freq = std::clamp(freq, 10.0, sr * 0.45);
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * 3.14159265358979 * freq / sr;
    const double cs = std::cos(w0), sn = std::sin(w0);
    const double alpha = sn / 2.0 * std::sqrt((A + 1.0 / A) * (1.0 / slope - 1.0) + 2.0);
    const double beta = 2.0 * std::sqrt(A) * alpha;
    const double a0 = (A + 1.0) + (A - 1.0) * cs + beta;
    BiquadCoefs c;
    c.b0 = static_cast<float>(A * ((A + 1.0) - (A - 1.0) * cs + beta) / a0);
    c.b1 = static_cast<float>(2.0 * A * ((A - 1.0) - (A + 1.0) * cs) / a0);
    c.b2 = static_cast<float>(A * ((A + 1.0) - (A - 1.0) * cs - beta) / a0);
    c.a1 = static_cast<float>(-2.0 * ((A - 1.0) + (A + 1.0) * cs) / a0);
    c.a2 = static_cast<float>(((A + 1.0) + (A - 1.0) * cs - beta) / a0);
    return c;
}

inline BiquadCoefs designHighShelf(double sr, double freq, double gainDb, double slope = 1.0) {
    freq = std::clamp(freq, 10.0, sr * 0.45);
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * 3.14159265358979 * freq / sr;
    const double cs = std::cos(w0), sn = std::sin(w0);
    const double alpha = sn / 2.0 * std::sqrt((A + 1.0 / A) * (1.0 / slope - 1.0) + 2.0);
    const double beta = 2.0 * std::sqrt(A) * alpha;
    const double a0 = (A + 1.0) - (A - 1.0) * cs + beta;
    BiquadCoefs c;
    c.b0 = static_cast<float>(A * ((A + 1.0) + (A - 1.0) * cs + beta) / a0);
    c.b1 = static_cast<float>(-2.0 * A * ((A - 1.0) + (A + 1.0) * cs) / a0);
    c.b2 = static_cast<float>(A * ((A + 1.0) + (A - 1.0) * cs - beta) / a0);
    c.a1 = static_cast<float>(2.0 * ((A - 1.0) - (A + 1.0) * cs) / a0);
    c.a2 = static_cast<float>(((A + 1.0) - (A - 1.0) * cs - beta) / a0);
    return c;
}

} // namespace od
