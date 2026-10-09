#include "GraphicEq.h"

#include <algorithm>
#include <cmath>

namespace psm {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr float kBypassThresholdDb = 0.05f;
} // namespace

BiquadCoeffs makePeakingCoeffs(float sampleRate, float freq, float q, float gainDb)
{
    // Computed in double: coefficients are only rebuilt when a gain changes, and low
    // bands at high sample rates lose precision in float.
    const double f = std::min<double>(freq, 0.45 * sampleRate);
    const double a = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * kPi * f / sampleRate;
    const double cosW0 = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * q);

    const double a0 = 1.0 + alpha / a;
    BiquadCoeffs c;
    c.b0 = static_cast<float>((1.0 + alpha * a) / a0);
    c.b1 = static_cast<float>((-2.0 * cosW0) / a0);
    c.b2 = static_cast<float>((1.0 - alpha * a) / a0);
    c.a1 = static_cast<float>((-2.0 * cosW0) / a0);
    c.a2 = static_cast<float>((1.0 - alpha / a) / a0);
    return c;
}

float biquadResponseDb(const BiquadCoeffs& c, float sampleRate, float freq)
{
    // phi-form of |H|^2 (RBJ cookbook): avoids the cancellation the cos(w) form suffers
    // at low frequencies, where b0+b1+b2 and 1+a1+a2 are tiny.
    const double s = std::sin(kPi * freq / sampleRate);
    const double phi = s * s;
    const double b0 = c.b0, b1 = c.b1, b2 = c.b2, a1 = c.a1, a2 = c.a2;
    const double bs = b0 + b1 + b2;
    const double as = 1.0 + a1 + a2;
    const double num = bs * bs - 4.0 * (b0 * b1 + 4.0 * b0 * b2 + b1 * b2) * phi + 16.0 * b0 * b2 * phi * phi;
    const double den = as * as - 4.0 * (a1 + 4.0 * a2 + a1 * a2) * phi + 16.0 * a2 * phi * phi;
    return static_cast<float>(10.0 * std::log10(std::max(num, 1e-30) / std::max(den, 1e-30)));
}

void GraphicEq::prepare(float sampleRate)
{
    sampleRate_ = sampleRate;
    reset();
    const EqGains gains = gains_;
    gains_.fill(0.0f);
    numActive_ = 0;
    setGains(gains);
}

void GraphicEq::reset()
{
    state_.fill(BandState{});
}

void GraphicEq::setGains(const EqGains& gainsDb)
{
    numActive_ = 0;
    for (int b = 0; b < kEqBands; ++b) {
        const float g = std::clamp(gainsDb[b], kEqMinGainDb, kEqMaxGainDb);
        const bool wasActive = std::fabs(gains_[b]) >= kBypassThresholdDb;
        const bool isActive = std::fabs(g) >= kBypassThresholdDb;
        if (g != gains_[b] || coeffs_[b].b0 == 1.0f) {
            coeffs_[b] = makePeakingCoeffs(sampleRate_, kEqBandFrequencies[b], kEqBandQ, g);
        }
        if (isActive && !wasActive) {
            state_[b] = BandState{}; // stale state from before the bypass would click
        }
        gains_[b] = g;
        if (isActive) {
            activeBands_[numActive_++] = static_cast<uint8_t>(b);
        }
    }
}

void GraphicEq::process(float* stereo, uint32_t frames)
{
    // Band-major loop: one filter's coefficients and state stay in registers for the whole block.
    for (int i = 0; i < numActive_; ++i) {
        const int b = activeBands_[i];
        const BiquadCoeffs c = coeffs_[b];
        BandState s = state_[b];
        float* p = stereo;
        for (uint32_t n = 0; n < frames; ++n, p += 2) {
            const float xl = p[0];
            const float xr = p[1];
            // Direct Form II Transposed.
            const float yl = c.b0 * xl + s.z1L;
            s.z1L = c.b1 * xl - c.a1 * yl + s.z2L;
            s.z2L = c.b2 * xl - c.a2 * yl;
            const float yr = c.b0 * xr + s.z1R;
            s.z1R = c.b1 * xr - c.a1 * yr + s.z2R;
            s.z2R = c.b2 * xr - c.a2 * yr;
            p[0] = yl;
            p[1] = yr;
        }
        state_[b] = s;
    }
}

float GraphicEq::responseDb(float freq) const
{
    float db = 0.0f;
    for (int i = 0; i < numActive_; ++i) {
        db += biquadResponseDb(coeffs_[activeBands_[i]], sampleRate_, freq);
    }
    return db;
}

} // namespace psm
