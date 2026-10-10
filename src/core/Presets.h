#pragma once

#include "GraphicEq.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace psm {

// App channels (what you listen to) and mic channels (your voice) need different curves,
// so each has its own preset list. A name is unique within its kind only.
enum class PresetKind : uint8_t { Output, Mic, Count };

struct Preset {
    std::string name;
    EqGains gainsDb{};
    bool builtIn = false;
    PresetKind kind = PresetKind::Output;
};

// Built-in presets can't be edited or renamed, but can be deleted (hidden) and restored;
// Flat always stays. User presets can be added, renamed and deleted freely. Stored as JSON.
class PresetLibrary {
public:
    PresetLibrary();

    // Every preset of both kinds; filter on Preset::kind.
    const std::vector<Preset>& presets() const { return presets_; }
    const Preset* find(const std::string& name, PresetKind kind) const;

    // Creates or overwrites a user preset. Fails for empty names and built-in names.
    bool save(const std::string& name, PresetKind kind, const EqGains& gainsDb, std::string* error);
    bool rename(const std::string& from, const std::string& to, PresetKind kind, std::string* error);
    bool remove(const std::string& name, PresetKind kind, std::string* error);
    bool canRemove(const std::string& name, PresetKind kind) const; // false for Flat and unknown names

    bool hasHiddenBuiltIns(PresetKind kind) const { return !hidden_[static_cast<int>(kind)].empty(); }
    void restoreBuiltIns(PresetKind kind); // brings back deleted built-ins whose name is still free

    // Only user presets and the names of deleted built-ins are written; built-ins come from code.
    bool loadUserPresets(const std::filesystem::path& file, std::string* error);
    bool saveUserPresets(const std::filesystem::path& file, std::string* error) const;

private:
    Preset* findMutable(const std::string& name, PresetKind kind);
    std::vector<Preset> presets_;
    std::array<std::vector<std::string>, static_cast<int>(PresetKind::Count)> hidden_;
};

} // namespace psm
