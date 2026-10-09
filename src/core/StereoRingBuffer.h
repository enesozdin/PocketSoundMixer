#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

namespace psm {

// Single-producer / single-consumer ring of interleaved stereo float frames.
// The producer is a capture thread, the consumer is the audio callback. Wait-free on both sides.
class StereoRingBuffer {
public:
    explicit StereoRingBuffer(uint32_t capacityFrames)
    {
        uint32_t cap = 1;
        while (cap < capacityFrames) cap <<= 1; // power of two: index wrap is a mask
        mask_ = cap - 1;
        data_.assign(static_cast<size_t>(cap) * 2, 0.0f);
    }

    uint32_t capacity() const { return mask_ + 1; }

    uint32_t available() const
    {
        return static_cast<uint32_t>(writePos_.load(std::memory_order_acquire) - readPos_.load(std::memory_order_acquire));
    }

    // Producer. Frames that do not fit are dropped; the capture thread never blocks.
    uint32_t write(const float* stereo, uint32_t frames)
    {
        const uint64_t w = writePos_.load(std::memory_order_relaxed);
        const uint64_t r = readPos_.load(std::memory_order_acquire);
        const uint32_t space = capacity() - static_cast<uint32_t>(w - r);
        const uint32_t n = std::min(frames, space);
        copyIn(w, stereo, n);
        writePos_.store(w + n, std::memory_order_release);
        return n;
    }

    // Producer: writes silence (WASAPI reports silent packets without data).
    uint32_t writeSilence(uint32_t frames)
    {
        const uint64_t w = writePos_.load(std::memory_order_relaxed);
        const uint64_t r = readPos_.load(std::memory_order_acquire);
        const uint32_t n = std::min(frames, capacity() - static_cast<uint32_t>(w - r));
        for (uint32_t i = 0; i < n; ++i) {
            const size_t idx = static_cast<size_t>((w + i) & mask_) * 2;
            data_[idx] = 0.0f;
            data_[idx + 1] = 0.0f;
        }
        writePos_.store(w + n, std::memory_order_release);
        return n;
    }

    // Consumer. Always fills `frames` (zero-padded on underrun). When the backlog exceeds
    // `maxLatencyFrames` it skips ahead to `targetLatencyFrames`, which bounds the delay when
    // the capture clock runs slightly faster than the output clock.
    uint32_t read(float* stereo, uint32_t frames, uint32_t targetLatencyFrames, uint32_t maxLatencyFrames)
    {
        uint64_t r = readPos_.load(std::memory_order_relaxed);
        const uint64_t w = writePos_.load(std::memory_order_acquire);
        uint32_t avail = static_cast<uint32_t>(w - r);
        if (avail > maxLatencyFrames + frames) {
            const uint32_t skip = avail - targetLatencyFrames - frames;
            r += skip;
            avail -= skip;
        }
        const uint32_t n = std::min(frames, avail);
        copyOut(r, stereo, n);
        if (n < frames) {
            std::memset(stereo + static_cast<size_t>(n) * 2, 0, static_cast<size_t>(frames - n) * 2 * sizeof(float));
        }
        readPos_.store(r + n, std::memory_order_release);
        return n;
    }

private:
    void copyIn(uint64_t pos, const float* src, uint32_t n)
    {
        const uint32_t start = static_cast<uint32_t>(pos & mask_);
        const uint32_t first = std::min(n, capacity() - start);
        std::memcpy(&data_[static_cast<size_t>(start) * 2], src, static_cast<size_t>(first) * 2 * sizeof(float));
        std::memcpy(&data_[0], src + static_cast<size_t>(first) * 2, static_cast<size_t>(n - first) * 2 * sizeof(float));
    }

    void copyOut(uint64_t pos, float* dst, uint32_t n) const
    {
        const uint32_t start = static_cast<uint32_t>(pos & mask_);
        const uint32_t first = std::min(n, capacity() - start);
        std::memcpy(dst, &data_[static_cast<size_t>(start) * 2], static_cast<size_t>(first) * 2 * sizeof(float));
        std::memcpy(dst + static_cast<size_t>(first) * 2, &data_[0], static_cast<size_t>(n - first) * 2 * sizeof(float));
    }

    std::vector<float> data_;
    uint32_t mask_ = 0;
    alignas(64) std::atomic<uint64_t> writePos_{0}; // separate cache lines: no false sharing
    alignas(64) std::atomic<uint64_t> readPos_{0};
};

} // namespace psm
