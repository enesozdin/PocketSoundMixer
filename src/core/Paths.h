#pragma once

#include <filesystem>

namespace psm {

// Per-user settings folder:
//   Windows: %APPDATA%\PocketSoundMixer
//   macOS:   ~/Library/Application Support/PocketSoundMixer
//   Linux:   $XDG_CONFIG_HOME/PocketSoundMixer or ~/.config/PocketSoundMixer
std::filesystem::path configDirectory();

} // namespace psm
