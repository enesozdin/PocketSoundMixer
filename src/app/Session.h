#pragma once

#include "GraphicEq.h"

#include <filesystem>
#include <string>
#include <vector>

namespace psm {

// One thing playing into a channel.
struct SourceConfig {
    std::string type;        // "app" | "input"
    std::string appExe;      // e.g. "Spotify.exe"
    std::string inputDevice; // empty = system default input (microphone)
};

// What the app restores on the next launch: the channel layout and every strip's settings.
struct ChannelConfig {
    std::string name;
    std::string preset = "Flat";
    EqGains gainsDb{};
    float volume = 1.0f; // linear, 0..1
    float pan = 0.0f;
    bool mute = false;
    bool solo = false;
    std::vector<SourceConfig> sources;
};

struct SessionConfig {
    float masterVolume = 1.0f;
    std::string outputDevice;    // where the mixer plays; empty = system default
    bool appAutoRoute = true;    // park captured apps' own output so they are heard once
    std::string appSilentOutput; // device name to park them on; empty = pick automatically
    std::vector<ChannelConfig> channels;
};

// Music, Game, Film, Chat and Podcast with their matching presets, plus a muted Mic channel.
SessionConfig defaultSession();

bool loadSession(const std::filesystem::path& file, SessionConfig& out, std::string* error);
bool saveSession(const std::filesystem::path& file, const SessionConfig& session, std::string* error);

} // namespace psm
