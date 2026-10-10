#pragma once

#include "AppCapture.h"
#include "AudioEngine.h"
#include "DeviceVolume.h"
#include "Presets.h"
#include "Session.h"
#include "Theme.h"

#include <array>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace psm {

// Immediate-mode UI for the mixer: the master section on top, one strip per channel below,
// plus a settings popup and a short help window.
// Runs entirely on the UI thread; talks to the audio thread only through Channel/Mixer atomics.
class MixerUI {
public:
    MixerUI(AudioEngine& engine, PresetLibrary& presets, std::filesystem::path presetFile);

    void applySession(const SessionConfig& session);
    SessionConfig captureSession() const;

    void draw(float deltaSeconds);

    // True while meters are moving, so the main loop keeps redrawing; otherwise it can sleep.
    bool isAnimating() const { return animating_; }

    void setStatus(std::string text) { status_ = std::move(text); }
    void setUiScale(float scale) { uiScale_ = scale; }
    // Windows volume of the Master output device, mirrored by the Master volume slider.
    void setDeviceVolume(DeviceVolume* volume) { deviceVolume_ = volume; }
    // Windows tray and autostart settings; called when the user flips them in Settings.
    void setSystemHooks(std::function<void(bool)> trayChanged, std::function<void(bool)> autostartChanged)
    {
        trayChanged_ = std::move(trayChanged);
        autostartChanged_ = std::move(autostartChanged);
    }
    bool trayEnabled() const { return trayEnabled_; }
    // Ends every app capture now, so apps are back on their own outputs (Windows shutdown).
    void releaseApps();

private:
    static constexpr int kCurvePoints = 64;

    // Peak meter with a short peak-hold line.
    struct Meter {
        float db = -90.0f;
        float holdDb = -90.0f;
        float holdSeconds = 0.0f;
    };

    struct Strip {
        Channel* channel = nullptr;
        bool isMic = false; // mic channel: one microphone instead of apps
        std::string outputDevice; // the user's choice, kept even while unplugged; empty = Automatic (Master)
        std::string preset = "Flat";
        bool presetModified = false;
        std::array<char, 64> nameBuf{};
        std::array<char, 64> presetNameBuf{};
        float volumePct = 100.0f;
        Meter meter[2];
        std::array<float, kCurvePoints> curve{};
        bool curveDirty = true;
        std::string error;
    };

    Strip* addStrip(const std::string& name, bool isMic);
    void removeStrip(size_t index);
    void addInput(Strip& strip, const std::string& deviceName);
    void addApp(Strip& strip, const std::string& exeName);
    void removeSource(Strip& strip, int slot);
    Strip* findAppStrip(const std::string& exeName, int* slot);
    std::string silentOutputId();
    void reopenAppChannels();
    void selectOutput(const std::string& name);
    void attachDeviceVolume();
    void applyChannelOutput(Strip& strip);
    void closeUnusedOutputs();
    std::vector<std::string> outputsInUse() const;
    void drawOutputRow(Strip& strip);
    void drawSpareOutputCombo();
    std::string spareOutputName();
    void applyPreset(Strip& strip, const Preset& preset);
    void updateCurve(Strip& strip);
    void savePresets();
    void deletePreset(const std::string& name);

    void drawMasterBar(float dt);
    void drawToolBar();
    void drawStrip(Strip& strip, size_t index, float dt);
    void drawEq(Strip& strip);
    void drawAppSources(Strip& strip);
    void drawMicSource(Strip& strip);
    void setTheme(Theme theme);
    void resetChannels();
    void drawPresetRow(Strip& strip);
    void drawHelp();
    void drawSettings();

    AudioEngine& engine_;
    PresetLibrary& presets_;
    std::filesystem::path presetFile_;
    std::vector<Strip> strips_;
    std::vector<std::string> captureDevices_;
    std::vector<std::string> playbackDevices_;
    std::vector<AudioAppInfo> audioApps_;
    std::array<char, 128> appExeBuf_{};
    std::vector<OutputDeviceInfo> outputDevices_;
    bool appAutoRoute_ = true;
    std::string appSilentOutput_; // device name; empty = automatic
    std::string status_;
    float masterVolumePct_ = 100.0f;
    Meter masterMeter_[2];
    bool showHelp_ = false;
    Theme theme_ = Theme::Dark;
    float uiScale_ = 1.0f;
    DeviceVolume* deviceVolume_ = nullptr;
    bool trayEnabled_ = true;
    bool startWithWindows_ = true;
    std::function<void(bool)> trayChanged_;
    std::function<void(bool)> autostartChanged_;
    std::string presetError_;
    int pendingRemove_ = -1;
    bool animating_ = false;
    bool scrollToNewStrip_ = false;
};

} // namespace psm
