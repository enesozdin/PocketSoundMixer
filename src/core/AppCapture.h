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

// True where per-app capture is implemented (Windows).
bool appCaptureSupported();

// Apps that currently have an audio session on any active output device.
std::vector<AudioAppInfo> listAudioApps();

// Opens the Windows "App volume and device preferences" page, where the user points an app
// at an unused output so its original sound does not play on top of the mixer.
void openAppVolumeSettings();

// Starts capturing `exeName`. Works even when the app is not running yet.
std::unique_ptr<AppSource> openAppCapture(const std::string& exeName, uint32_t sampleRate, std::string* error);

} // namespace psm
