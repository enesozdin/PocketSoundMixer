#pragma once

#include <cstdint>
#include <string>

namespace psm {

// Base for objects shared between the UI thread and the audio thread.
// They are never deleted directly: the Mixer retires them and frees them
// once the audio thread can no longer hold a pointer to them.
class Retirable {
public:
    virtual ~Retirable() = default;
};

enum class SourceKind : uint8_t { File, Input, App, Other };

// A stereo float producer pulled by the audio thread.
class AudioSource : public Retirable {
public:
    // Audio thread. Must not block, lock or allocate.
    // Writes exactly `frames` interleaved stereo frames (zero-fills what it cannot produce).
    virtual void read(float* outStereo, uint32_t frames) = 0;
    virtual SourceKind kind() const = 0;
};

// Plays a WAV / MP3 / FLAC file, streamed and decoded off the audio thread.
class FileSource : public AudioSource {
public:
    SourceKind kind() const override { return SourceKind::File; }

    // UI thread, lock-free.
    virtual const std::string& path() const = 0;
    virtual bool isPlaying() const = 0;
    virtual void setPlaying(bool playing) = 0;
    virtual bool isLooping() const = 0;
    virtual void setLooping(bool looping) = 0;
    virtual uint64_t lengthFrames() const = 0;   // 0 when unknown
    virtual uint64_t cursorFrames() const = 0;
    virtual void requestSeek(uint64_t frame) = 0;
};

// Live audio from a capture device (mic, line-in, audio interface).
class InputSource : public AudioSource {
public:
    SourceKind kind() const override { return SourceKind::Input; }
    virtual const std::string& deviceName() const = 0;
};

// Captures the sound of one application (and its child processes), e.g. Spotify or a game.
// Windows only for now (WASAPI process loopback). If the app is not running, the source
// waits and attaches by itself once it starts.
class AppSource : public AudioSource {
public:
    enum class State : uint8_t { WaitingForApp, Capturing, Failed };

    SourceKind kind() const override { return SourceKind::App; }
    virtual const std::string& exeName() const = 0; // e.g. "Spotify.exe"
    virtual State state() const = 0;
    virtual std::string lastError() const = 0;      // UI thread
};

} // namespace psm
