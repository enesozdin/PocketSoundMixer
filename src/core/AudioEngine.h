#pragma once

#include "AudioSource.h"
#include "Mixer.h"

#include <memory>
#include <string>
#include <vector>

namespace psm {

// Owns the output device and the Mixer.
// miniaudio stays hidden behind a pimpl so only one translation unit pays its compile cost.
class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    // Opens the output device and starts the audio thread. `outputDevice` empty = system default,
    // which also follows Windows when the default changes. If the named device is gone, the
    // default is used. On failure the Mixer still exists (at 48 kHz) so the UI keeps working.
    bool start(const std::string& outputDevice, std::string* error);
    void stop();
    bool isRunning() const;

    Mixer& mixer();
    uint32_t sampleRate() const;
    const std::string& outputDeviceName() const; // the device actually playing
    const std::string& requestedOutput() const;  // empty = system default

    // UI thread.
    std::vector<std::string> outputDeviceNames();  // refreshes the device list
    // Moves the mixer to another output (empty = system default). Channels keep playing.
    bool setOutputDevice(const std::string& name, std::string* error);
    std::vector<std::string> captureDeviceNames(); // refreshes the device list

    // Mixer output index for a device a channel should play on: 0 = the Master device
    // (also for an empty name). Opens an extra device on first use, up to Mixer::kMaxOutputs - 1.
    // On failure returns 0 and sets `error`, so the channel keeps playing on Master.
    int outputFor(const std::string& deviceName, std::string* error);
    // Closes the extra devices whose index is not in `inUse`.
    void closeUnusedOutputs(const std::vector<int>& inUse);
    std::unique_ptr<InputSource> openInput(const std::string& deviceName, std::string* error);
    // Windows only. `silentOutputId`: where the app's own output is parked while captured (empty = leave it).
    std::unique_ptr<AppSource> openApp(const std::string& exeName, const std::string& silentOutputId, std::string* error);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Flushes denormals to zero on the calling thread. IIR filters decaying towards silence
// otherwise produce denormal floats, which are up to 100x slower on x86.
void enableFlushDenormalsToZero();

} // namespace psm
