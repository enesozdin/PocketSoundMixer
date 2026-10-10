#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace psm {

// The volume Windows keeps for one output device: the one the taskbar slider and headset
// buttons change. The mixer's Master volume mirrors it, so both always show the same value.
// Windows reports changes on its own thread; they land in atomics, so reading is free.
// Elsewhere (macOS, Linux) it is never attached and the Master volume stays a mixer gain.
class DeviceVolume {
public:
    // `onChange` runs on a Windows thread when the volume or mute changes outside the app,
    // e.g. to wake the UI. Keep it cheap and thread-safe.
    explicit DeviceVolume(std::function<void()> onChange = {});
    ~DeviceVolume();
    DeviceVolume(const DeviceVolume&) = delete;
    DeviceVolume& operator=(const DeviceVolume&) = delete;

    // Follows `deviceName` (a friendly name as the engine lists it); empty = the system default,
    // re-attached whenever Windows changes its default device. False if the device isn't found.
    bool attach(const std::string& deviceName);
    void detach();
    bool attached() const { return attached_.load(std::memory_order_relaxed); }

    // Call once per frame on the UI thread: re-attaches after a default-device change.
    void update();

    float level() const { return level_.load(std::memory_order_relaxed); } // 0..1, as Windows shows it
    bool muted() const { return muted_.load(std::memory_order_relaxed); }
    void setLevel(float level);
    void setMuted(bool muted);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::function<void()> onChange_;
    std::string deviceName_;
    std::atomic<bool> attached_{false};
    std::atomic<bool> defaultChanged_{false};
    std::atomic<float> level_{1.0f};
    std::atomic<bool> muted_{false};
};

} // namespace psm
