#pragma once

#include "GraphicEq.h"

#include <filesystem>
#include <string>
#include <vector>

namespace psm {

// What the app restores on the next launch: the channel layout and every strip's settings.
struct ChannelConfig {
    std::string name;
    std::string preset = "Flat";
    EqGains gainsDb{};
    float volume = 1.0f;
    float pan = 0.0f;
    bool mute = false;
    bool solo = false;
    std::string sourceType = "none"; // "none" | "file" | "input" | "app"
    std::string filePath;            // UTF-8
    bool loop = true;
    std::string inputDevice;         // empty = system default input
    std::string appExe;              // e.g. "Spotify.exe"
};

struct SessionConfig {
    float masterVolume = 1.0f;
    std::vector<ChannelConfig> channels;
};

// Music, Game, Film, Chat and Podcast, each with its matching preset.
SessionConfig defaultSession();

bool loadSession(const std::filesystem::path& file, SessionConfig& out, std::string* error);
bool saveSession(const std::filesystem::path& file, const SessionConfig& session, std::string* error);

} // namespace psm
