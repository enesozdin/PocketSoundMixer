#pragma once

#include "AudioSource.h"

#include <memory>
#include <string>
#include <vector>

namespace psm {

struct AudioAppInfo {
    std::string exeName;     // "Spotify.exe", used to find the app again on the next launch
    std::string displayName; // "Spotify"
};

struct OutputDeviceInfo {
    std::string id;   // Windows endpoint id
    std::string name; // "Speakers (Realtek(R) Audio)"
    bool isDefault = false;
};

// True where per-app capture is implemented (Windows).
bool appCaptureSupported();

// Active output devices.
std::vector<OutputDeviceInfo> listOutputDevices();

// A device the user is unlikely to listen to, for parking captured apps: the first active
// output that is neither the system default nor `mixerOutputName` (where the mixer plays).
// Empty when there is no such device.
std::string pickSilentOutputId(const std::vector<OutputDeviceInfo>& devices, const std::string& mixerOutputName);

// Apps that currently have an audio session on any active output device.
std::vector<AudioAppInfo> listAudioApps();

// Opens the Windows "App volume and device preferences" page, where the user points an app
// at an unused output so its original sound does not play on top of the mixer.
void openAppVolumeSettings();

// Escape hatch: puts every app back on the system default output (same as "Reset" in
// Windows' App volume and device preferences).
void resetAllAppOutputs();

// Starts capturing `exeName`. Works even when the app is not running yet.
// When `silentOutputId` is set, the app's own output is moved there while it is captured, so it
// is heard only through the mixer; the app's previous output is restored when capture stops.
std::unique_ptr<AppSource> openAppCapture(const std::string& exeName, uint32_t sampleRate,
                                          const std::string& silentOutputId, std::string* error);

} // namespace psm
