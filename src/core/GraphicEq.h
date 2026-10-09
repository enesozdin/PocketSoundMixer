#pragma once

#include <array>
#include <cstdint>

namespace psm {

constexpr int kEqBands = 10;
constexpr float kEqMinGainDb = -12.0f;
constexpr float kEqMaxGainDb = 12.0f;
constexpr float kEqBandQ = 1.41f; // ~1 octave bandwidth, standard for octave-spaced graphic EQs

constexpr std::array<float, kEqBands> kEqBandFrequencies{
    31.5f, 63.0f, 125.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f};
constexpr std::array<const char*, kEqBands> kEqBandLabels{
    "31", "63", "125", "250", "500", "1k", "2k", "4k", "8k", "16k"};

using EqGains = std::array<float, kEqBands>;

struct BiquadCoeffs {
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
};

// RBJ Audio EQ Cookbook peaking filter, normalized so a0 == 1.
BiquadCoeffs makePeakingCoeffs(float sampleRate, float freq, float q, float gainDb);

// Magnitude of one biquad at `freq`, in dB.
float biquadResponseDb(const BiquadCoeffs& c, float sampleRate, float freq);

// 10-band graphic equalizer for interleaved stereo float audio.
// Audio thread only. No allocations; bands at 0 dB are skipped entirely.
class GraphicEq {
public:
    void prepare(float sampleRate);
    void setGains(const EqGains& gainsDb);
    void reset();
    void process(float* stereo, uint32_t frames);

    // Summed magnitude response in dB at `freq` (for UI curves and tests).
    float responseDb(float freq) const;
    int activeBandCount() const { return numActive_; }

private:
    struct BandState {
        float z1L = 0.0f, z2L = 0.0f, z1R = 0.0f, z2R = 0.0f;
    };

    float sampleRate_ = 48000.0f;
    EqGains gains_{};
    std::array<BiquadCoeffs, kEqBands> coeffs_{};
    std::array<BandState, kEqBands> state_{};
    std::array<uint8_t, kEqBands> activeBands_{};
    int numActive_ = 0;
};

} // namespace psm
