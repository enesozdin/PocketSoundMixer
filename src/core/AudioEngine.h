#pragma once

#include "AudioSource.h"
#include "Mixer.h"

#include <memory>
#include <string>
#include <vector>

namespace psm {

// Owns the output device, the file streaming backend and the Mixer.
// miniaudio stays hidden behind a pimpl so only one translation unit pays its compile cost.
class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    // Opens the default output device and starts the audio thread.
    // On failure the Mixer still exists (at 48 kHz) so the UI keeps working.
    bool start(std::string* error);
    void stop();
    bool isRunning() const;

    Mixer& mixer();
    uint32_t sampleRate() const;
    const std::string& outputDeviceName() const;

    // UI thread.
    std::vector<std::string> captureDeviceNames(); // refreshes the device list
    std::unique_ptr<FileSource> openFile(const std::string& utf8Path, std::string* error);
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
