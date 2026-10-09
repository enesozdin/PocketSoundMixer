#pragma once

#include "AudioEngine.h"
#include "Presets.h"
#include "Session.h"

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace psm {

// Immediate-mode UI for the mixer: one strip per channel, a master section and a preset manager.
// Runs entirely on the UI thread; talks to the audio thread only through Channel/Mixer atomics.
class MixerUI {
public:
    MixerUI(AudioEngine& engine, PresetLibrary& presets, std::filesystem::path presetFile);

    void applySession(const SessionConfig& session);
    SessionConfig captureSession() const;

    void draw(float deltaSeconds);

    // GLFW drop callback: a file dropped on a strip loads into it, anywhere else adds a channel.
    void onFilesDropped(const std::vector<std::string>& utf8Paths, float x, float y);

    // True while meters are moving, so the main loop keeps redrawing; otherwise it can sleep.
    bool isAnimating() const { return animating_; }

    void setStatus(std::string text) { status_ = std::move(text); }

private:
    static constexpr int kCurvePoints = 64;

    struct Strip {
        Channel* channel = nullptr;
        std::string preset = "Flat";
        bool presetModified = false;
        std::array<char, 64> nameBuf{};
        std::array<char, 512> pathBuf{};
        std::array<char, 64> presetNameBuf{};
        float volumeDb = 0.0f;
        float meterDb[2] = {-90.0f, -90.0f};
        std::array<float, kCurvePoints> curve{};
        bool curveDirty = true;
        std::string error;
        float rectMin[2] = {0, 0};
        float rectMax[2] = {0, 0};
    };

    Strip* addStrip(const std::string& name);
    void removeStrip(size_t index);
    void loadFile(Strip& strip, const std::string& utf8Path, bool loop);
    void loadInput(Strip& strip, const std::string& deviceName);
    void applyPreset(Strip& strip, const Preset& preset);
    void updateCurve(Strip& strip);
    void savePresets();

    void drawTopBar(float dt);
    void drawStrip(Strip& strip, size_t index, float dt);
    void drawEq(Strip& strip);
    void drawSourceRow(Strip& strip);
    void drawPresetRow(Strip& strip);
    void drawPresetManager();

    AudioEngine& engine_;
    PresetLibrary& presets_;
    std::filesystem::path presetFile_;
    std::vector<Strip> strips_;
    std::vector<std::string> captureDevices_;
    std::string status_;
    float masterVolumeDb_ = 0.0f;
    float masterMeterDb_[2] = {-90.0f, -90.0f};
    bool showPresetManager_ = false;
    std::string selectedPreset_ = "Flat";
    std::array<char, 64> renameBuf_{};
    std::string presetError_;
    int pendingRemove_ = -1;
    bool animating_ = false;
    bool scrollToNewStrip_ = false;
};

} // namespace psm
