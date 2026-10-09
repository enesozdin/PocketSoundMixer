#include "Channel.h"

#include <algorithm>
#include <cmath>

namespace psm {

Channel::Channel(std::string name, float sampleRate)
    : name_(std::move(name))
{
    for (auto& g : gains_) {
        g.store(0.0f, std::memory_order_relaxed);
    }
    eq_.prepare(sampleRate);
}

Channel::~Channel()
{
    delete source_.load(std::memory_order_acquire);
}

void Channel::setGains(const EqGains& gainsDb)
{
    for (int b = 0; b < kEqBands; ++b) {
        gains_[b].store(gainsDb[b], std::memory_order_relaxed);
    }
    gainsVersion_.fetch_add(1, std::memory_order_release);
}

void Channel::setGain(int band, float gainDb)
{
    gains_[band].store(gainDb, std::memory_order_relaxed);
    gainsVersion_.fetch_add(1, std::memory_order_release);
}

EqGains Channel::gains() const
{
    EqGains out{};
    for (int b = 0; b < kEqBands; ++b) {
        out[b] = gains_[b].load(std::memory_order_relaxed);
    }
    return out;
}

std::unique_ptr<AudioSource> Channel::exchangeSource(std::unique_ptr<AudioSource> source)
{
    return std::unique_ptr<AudioSource>(source_.exchange(source.release(), std::memory_order_acq_rel));
}

void Channel::process(float* mixBus, float* scratch, uint32_t frames, bool anySolo)
{
    const uint32_t version = gainsVersion_.load(std::memory_order_acquire);
    if (version != appliedGainsVersion_) {
        // Coefficients are rebuilt only when a slider moved, never per sample.
        eq_.setGains(gains());
        appliedGainsVersion_ = version;
    }

    const bool audible = !mute.load(std::memory_order_relaxed)
                      && (!anySolo || solo.load(std::memory_order_relaxed));
    const float vol = audible ? volume.load(std::memory_order_relaxed) : 0.0f;
    const float p = std::clamp(pan.load(std::memory_order_relaxed), -1.0f, 1.0f);
    const float targetL = vol * std::min(1.0f, 1.0f - p);
    const float targetR = vol * std::min(1.0f, 1.0f + p);

    AudioSource* src = source_.load(std::memory_order_acquire);
    if (src == nullptr) {
        currentGainL_ = targetL;
        currentGainR_ = targetR;
        return;
    }

    // Always pull the source so file playback keeps time while muted.
    src->read(scratch, frames);

    if (targetL == 0.0f && targetR == 0.0f && currentGainL_ == 0.0f && currentGainR_ == 0.0f) {
        return; // silent: skip the EQ and the mix
    }

    eq_.process(scratch, frames);

    // Linear gain ramp across the block avoids zipper noise when a fader moves.
    const float invFrames = 1.0f / static_cast<float>(frames);
    const float stepL = (targetL - currentGainL_) * invFrames;
    const float stepR = (targetR - currentGainR_) * invFrames;
    float gl = currentGainL_;
    float gr = currentGainR_;
    float peakL = 0.0f;
    float peakR = 0.0f;
    for (uint32_t n = 0; n < frames; ++n) {
        gl += stepL;
        gr += stepR;
        const float l = scratch[2 * n] * gl;
        const float r = scratch[2 * n + 1] * gr;
        mixBus[2 * n] += l;
        mixBus[2 * n + 1] += r;
        peakL = std::max(peakL, std::fabs(l));
        peakR = std::max(peakR, std::fabs(r));
    }
    currentGainL_ = targetL;
    currentGainR_ = targetR;

    if (peakL > peak_[0].load(std::memory_order_relaxed)) {
        peak_[0].store(peakL, std::memory_order_relaxed);
    }
    if (peakR > peak_[1].load(std::memory_order_relaxed)) {
        peak_[1].store(peakR, std::memory_order_relaxed);
    }
}

} // namespace psm
