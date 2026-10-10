#include "Mixer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

namespace psm {

Mixer::Mixer(float sampleRate)
    : sampleRate_(sampleRate)
    , scratch_(static_cast<size_t>(kMaxBlockFrames) * 4, 0.0f)
    , busScratch_(static_cast<size_t>(kMaxBlockFrames) * 2 * kMaxOutputs, 0.0f)
{
    for (auto& s : slots_) {
        s.store(nullptr, std::memory_order_relaxed);
    }
    for (auto& b : buses_) {
        b.store(nullptr, std::memory_order_relaxed);
    }
}

Mixer::~Mixer() // every audio device must be stopped before this runs
{
    for (auto& b : buses_) {
        delete b.load(std::memory_order_acquire);
    }
}

void Mixer::setOutputBus(int index, std::unique_ptr<OutputBus> bus)
{
    if (index <= 0 || index >= kMaxOutputs) return;
    if (OutputBus* old = buses_[index].exchange(bus.release(), std::memory_order_acq_rel)) {
        retire(std::unique_ptr<Retirable>(old));
    }
}

Channel* Mixer::addChannel(std::string name)
{
    for (auto& slot : slots_) {
        if (slot.load(std::memory_order_relaxed) == nullptr) {
            auto channel = std::make_unique<Channel>(std::move(name), sampleRate_);
            Channel* raw = channel.get();
            owned_.push_back(std::move(channel));
            order_.push_back(raw);
            slot.store(raw, std::memory_order_release); // publish fully constructed channel
            return raw;
        }
    }
    return nullptr;
}

void Mixer::removeChannel(Channel* channel)
{
    for (auto& slot : slots_) {
        if (slot.load(std::memory_order_relaxed) == channel) {
            slot.store(nullptr, std::memory_order_seq_cst);
            break;
        }
    }
    order_.erase(std::remove(order_.begin(), order_.end(), channel), order_.end());
    auto it = std::find_if(owned_.begin(), owned_.end(), [channel](const auto& p) { return p.get() == channel; });
    if (it != owned_.end()) {
        std::unique_ptr<Retirable> retired = std::move(*it);
        owned_.erase(it);
        retire(std::move(retired));
    }
}

bool Mixer::addSource(Channel* channel, std::unique_ptr<AudioSource> source)
{
    const int slot = channel->freeSourceSlot();
    if (slot < 0) return false;
    replaceSource(channel, slot, std::move(source));
    return true;
}

void Mixer::replaceSource(Channel* channel, int slot, std::unique_ptr<AudioSource> source)
{
    if (auto old = channel->exchangeSource(slot, std::move(source))) {
        retire(std::move(old));
    }
}

void Mixer::clearSources(Channel* channel)
{
    for (int i = 0; i < Channel::kMaxSources; ++i) {
        replaceSource(channel, i, nullptr);
    }
}

void Mixer::retire(std::unique_ptr<Retirable> object)
{
    // A callback that loaded the old pointer was already running when we read this epoch;
    // it bumps the epoch when it returns. Waiting for +2 leaves one full callback of margin.
    graveyard_.push_back({std::move(object), epoch_.load(std::memory_order_seq_cst) + 2});
}

void Mixer::collectGarbage()
{
    const uint64_t now = epoch_.load(std::memory_order_acquire);
    graveyard_.erase(std::remove_if(graveyard_.begin(), graveyard_.end(),
                                    [now](const Retired& r) { return now >= r.epoch; }),
                     graveyard_.end());
}

void Mixer::flushGarbage(int timeoutMs)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    collectGarbage();
    while (!graveyard_.empty() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2)); // ~1 audio callback
        collectGarbage();
    }
}

void Mixer::process(float* out, uint32_t frames)
{
    float* scratch = scratch_.data();
    float* temp = scratch + static_cast<size_t>(kMaxBlockFrames) * 2;
    uint32_t done = 0;
    while (done < frames) {
        const uint32_t n = std::min(kMaxBlockFrames, frames - done);
        const size_t samples = static_cast<size_t>(n) * 2;

        // Mix targets for this block: the Master device plus each extra output that exists.
        std::array<OutputBus*, kMaxOutputs> bus{};
        std::array<float*, kMaxOutputs> target{};
        target[0] = out + static_cast<size_t>(done) * 2;
        for (int k = 1; k < kMaxOutputs; ++k) {
            bus[k] = buses_[k].load(std::memory_order_acquire);
            target[k] = bus[k] ? busScratch_.data() + static_cast<size_t>(k) * kMaxBlockFrames * 2 : nullptr;
        }
        for (int k = 0; k < kMaxOutputs; ++k) {
            if (target[k]) std::fill(target[k], target[k] + samples, 0.0f);
        }

        bool anySolo = false;
        for (const auto& slot : slots_) {
            const Channel* c = slot.load(std::memory_order_acquire);
            if (c != nullptr && c->solo.load(std::memory_order_relaxed)) {
                anySolo = true;
                break;
            }
        }
        for (const auto& slot : slots_) {
            if (Channel* c = slot.load(std::memory_order_acquire)) {
                const int o = c->output.load(std::memory_order_relaxed);
                float* dst = (o > 0 && o < kMaxOutputs && target[o]) ? target[o] : target[0];
                c->process(dst, scratch, temp, n, anySolo);
            }
        }

        const float targetGain = masterVolume.load(std::memory_order_relaxed);
        const float step = (targetGain - currentMaster_) / static_cast<float>(n);
        float peakL = 0.0f;
        float peakR = 0.0f;
        for (int k = 0; k < kMaxOutputs; ++k) {
            float* block = target[k];
            if (!block) continue;
            float g = currentMaster_;
            for (uint32_t i = 0; i < n; ++i) {
                g += step;
                // Hard clamp protects ears and speakers; the meters show when it kicks in.
                const float l = std::clamp(block[2 * i] * g, -1.0f, 1.0f);
                const float r = std::clamp(block[2 * i + 1] * g, -1.0f, 1.0f);
                block[2 * i] = l;
                block[2 * i + 1] = r;
                peakL = std::max(peakL, std::fabs(l));
                peakR = std::max(peakR, std::fabs(r));
            }
            if (k > 0) bus[k]->ring.write(block, n); // drops what doesn't fit; never blocks
        }
        currentMaster_ = targetGain;
        if (peakL > masterPeak_[0].load(std::memory_order_relaxed)) {
            masterPeak_[0].store(peakL, std::memory_order_relaxed);
        }
        if (peakR > masterPeak_[1].load(std::memory_order_relaxed)) {
            masterPeak_[1].store(peakR, std::memory_order_relaxed);
        }
        done += n;
    }
    epoch_.fetch_add(1, std::memory_order_release);
}

} // namespace psm
