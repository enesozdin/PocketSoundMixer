#pragma once

#include "AudioSource.h"
#include "GraphicEq.h"

#include <array>
#include <atomic>
#include <memory>
#include <string>

namespace psm {

// One mixer strip: sources (summed) -> 10-band EQ -> volume / balance -> mix bus.
// Parameters are atomics written by the UI thread and read by the audio thread.
class Channel : public Retirable {
public:
    Channel(std::string name, float sampleRate);
    ~Channel() override;

    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;

    // ---- UI thread ----
    const std::string& name() const { return name_; }
    void setName(std::string name) { name_ = std::move(name); }

    void setGains(const EqGains& gainsDb);
    void setGain(int band, float gainDb);
    EqGains gains() const;

    std::atomic<float> volume{1.0f}; // linear
    std::atomic<float> pan{0.0f};    // balance, -1 (left) .. +1 (right)
    std::atomic<bool> mute{false};
    std::atomic<bool> solo{false};
    std::atomic<int> output{0};      // Mixer output index: 0 = Master device, 1.. = extra devices

    // Post-fader peak since the last call, then cleared. side: 0 = left, 1 = right.
    // Measured even while muted, so a muted mic still shows that it hears you.
    float takePeak(int side) { return peak_[side].exchange(0.0f, std::memory_order_relaxed); }

    // Several apps (or a mic) can play into one channel; their sound is summed before the EQ.
    static constexpr int kMaxSources = 8;
    AudioSource* source(int slot) const { return sources_[slot].load(std::memory_order_acquire); }
    int sourceCount() const;
    int freeSourceSlot() const; // -1 when all slots are taken
    // Swaps in a new source and returns the old one, which the caller must retire through the Mixer.
    std::unique_ptr<AudioSource> exchangeSource(int slot, std::unique_ptr<AudioSource> source);

    // ---- Audio thread ----
    // Adds this channel's output to `mixBus`. `scratch` and `temp` each hold at least `frames` stereo frames.
    void process(float* mixBus, float* scratch, float* temp, uint32_t frames, bool anySolo);

private:
    std::string name_;
    std::array<std::atomic<float>, kEqBands> gains_;
    std::atomic<uint32_t> gainsVersion_{1};
    std::array<std::atomic<AudioSource*>, kMaxSources> sources_{};
    std::atomic<float> peak_[2] = {0.0f, 0.0f};

    // Audio thread state.
    GraphicEq eq_;
    uint32_t appliedGainsVersion_ = 0;
    float currentGainL_ = 0.0f; // ramps from silence, so a new channel fades in
    float currentGainR_ = 0.0f;
};

} // namespace psm
