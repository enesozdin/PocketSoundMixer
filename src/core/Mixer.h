#pragma once

#include "Channel.h"

#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace psm {

// Owns the channels and sums them into the output.
//
// Threading: add/remove/replace and collectGarbage run on the UI thread; process runs
// on the audio thread. The audio thread sees channels through a fixed array of atomic
// slots, so it never locks or allocates. Removed objects go to a graveyard and are
// deleted only after the audio thread has finished at least one more callback.
class Mixer {
public:
    static constexpr int kMaxChannels = 64;
    static constexpr uint32_t kMaxBlockFrames = 1024;

    explicit Mixer(float sampleRate);
    ~Mixer();

    Mixer(const Mixer&) = delete;
    Mixer& operator=(const Mixer&) = delete;

    float sampleRate() const { return sampleRate_; }

    // ---- UI thread ----
    Channel* addChannel(std::string name); // nullptr when kMaxChannels is reached
    void removeChannel(Channel* channel);
    // Puts `source` in the channel's first free slot. Returns false (and drops it) when all are taken.
    bool addSource(Channel* channel, std::unique_ptr<AudioSource> source);
    void replaceSource(Channel* channel, int slot, std::unique_ptr<AudioSource> source);
    void clearSources(Channel* channel);
    void collectGarbage();                 // call once per UI frame
    // Blocks (up to timeoutMs) until everything retired so far is freed. For the rare case where
    // an old object's destructor must finish before its replacement starts.
    void flushGarbage(int timeoutMs);

    const std::vector<Channel*>& channels() const { return order_; }

    std::atomic<float> masterVolume{1.0f};
    float takeMasterPeak(int side) { return masterPeak_[side].exchange(0.0f, std::memory_order_relaxed); }

    // ---- Audio thread ----
    // Writes `frames` interleaved stereo frames to `out` (overwrites, does not accumulate).
    void process(float* out, uint32_t frames);

private:
    void retire(std::unique_ptr<Retirable> object);

    struct Retired {
        std::unique_ptr<Retirable> object;
        uint64_t epoch;
    };

    float sampleRate_;
    std::array<std::atomic<Channel*>, kMaxChannels> slots_;
    std::vector<std::unique_ptr<Channel>> owned_;
    std::vector<Channel*> order_;
    std::vector<Retired> graveyard_;
    std::atomic<uint64_t> epoch_{0};
    std::atomic<float> masterPeak_[2] = {0.0f, 0.0f};

    // Audio thread state.
    std::vector<float> scratch_; // two blocks: channel sum + one source
    float currentMaster_ = 0.0f;
};

} // namespace psm
