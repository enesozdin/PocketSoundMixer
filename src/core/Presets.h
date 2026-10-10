#pragma once

#include "GraphicEq.h"

#include <filesystem>
#include <string>
#include <vector>

namespace psm {

struct Preset {
    std::string name;
    EqGains gainsDb{};
    bool builtIn = false;
};

// Built-in presets can't be edited or renamed, but can be deleted (hidden) and restored;
// Flat always stays. User presets can be added, renamed and deleted freely. Stored as JSON.
class PresetLibrary {
public:
    PresetLibrary();

    const std::vector<Preset>& presets() const { return presets_; }
    const Preset* find(const std::string& name) const;

    // Creates or overwrites a user preset. Fails for empty names and built-in names.
    bool save(const std::string& name, const EqGains& gainsDb, std::string* error);
    bool rename(const std::string& from, const std::string& to, std::string* error);
    bool remove(const std::string& name, std::string* error);
    bool canRemove(const std::string& name) const; // false for Flat and unknown names

    bool hasHiddenBuiltIns() const { return !hiddenBuiltIns_.empty(); }
    void restoreBuiltIns(); // brings back deleted built-ins whose name is still free

    // Only user presets and the names of deleted built-ins are written; built-ins come from code.
    bool loadUserPresets(const std::filesystem::path& file, std::string* error);
    bool saveUserPresets(const std::filesystem::path& file, std::string* error) const;

private:
    Preset* findMutable(const std::string& name);
    std::vector<Preset> presets_;
    std::vector<std::string> hiddenBuiltIns_;
};

} // namespace psm
