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
    for (auto& s : sources_) {
        s.store(nullptr, std::memory_order_relaxed);
    }
    eq_.prepare(sampleRate);
}

Channel::~Channel()
{
    for (auto& s : sources_) {
        delete s.load(std::memory_order_acquire);
    }
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

int Channel::sourceCount() const
{
    int n = 0;
    for (const auto& s : sources_) {
        n += s.load(std::memory_order_relaxed) != nullptr ? 1 : 0;
    }
    return n;
}

int Channel::freeSourceSlot() const
{
    for (int i = 0; i < kMaxSources; ++i) {
        if (sources_[i].load(std::memory_order_relaxed) == nullptr) return i;
    }
    return -1;
}

std::unique_ptr<AudioSource> Channel::exchangeSource(int slot, std::unique_ptr<AudioSource> source)
{
    return std::unique_ptr<AudioSource>(sources_[slot].exchange(source.release(), std::memory_order_acq_rel));
}

void Channel::process(float* mixBus, float* scratch, float* temp, uint32_t frames, bool anySolo)
{
    const uint32_t version = gainsVersion_.load(std::memory_order_acquire);
    if (version != appliedGainsVersion_) {
        // Coefficients are rebuilt only when a slider moved, never per sample.
        eq_.setGains(gains());
        appliedGainsVersion_ = version;
    }

    const bool audible = !mute.load(std::memory_order_relaxed)
                      && (!anySolo || solo.load(std::memory_order_relaxed));
    const float vol = volume.load(std::memory_order_relaxed);
    const float p = std::clamp(pan.load(std::memory_order_relaxed), -1.0f, 1.0f);
    const float panL = std::min(1.0f, 1.0f - p);
    const float panR = std::min(1.0f, 1.0f + p);
    const float targetL = audible ? vol * panL : 0.0f;
    const float targetR = audible ? vol * panR : 0.0f;

    // Sum every source into scratch. Sources are always pulled, even while muted,
    // so live captures never pile up a backlog.
    const size_t samples = static_cast<size_t>(frames) * 2;
    int count = 0;
    for (auto& slot : sources_) {
        AudioSource* src = slot.load(std::memory_order_acquire);
        if (src == nullptr) continue;
        if (count == 0) {
            src->read(scratch, frames);
        } else {
            src->read(temp, frames);
            for (size_t i = 0; i < samples; ++i) {
                scratch[i] += temp[i];
            }
        }
        ++count;
    }
    if (count == 0) {
        currentGainL_ = targetL;
        currentGainR_ = targetR;
        return;
    }

    float peakL = 0.0f;
    float peakR = 0.0f;
    if (targetL == 0.0f && targetR == 0.0f && currentGainL_ == 0.0f && currentGainR_ == 0.0f) {
        // Silent: skip the EQ and the mix, but keep the meter alive (pre-EQ, cheap).
        for (uint32_t n = 0; n < frames; ++n) {
            peakL = std::max(peakL, std::fabs(scratch[2 * n]));
            peakR = std::max(peakR, std::fabs(scratch[2 * n + 1]));
        }
        peakL *= vol * panL;
        peakR *= vol * panR;
    } else {
        eq_.process(scratch, frames);

        // Linear gain ramp across the block avoids zipper noise when a fader moves.
        const float invFrames = 1.0f / static_cast<float>(frames);
        const float stepL = (targetL - currentGainL_) * invFrames;
        const float stepR = (targetR - currentGainR_) * invFrames;
        float gl = currentGainL_;
        float gr = currentGainR_;
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
    }

    if (peakL > peak_[0].load(std::memory_order_relaxed)) {
        peak_[0].store(peakL, std::memory_order_relaxed);
    }
    if (peakR > peak_[1].load(std::memory_order_relaxed)) {
        peak_[1].store(peakR, std::memory_order_relaxed);
    }
}

} // namespace psm
