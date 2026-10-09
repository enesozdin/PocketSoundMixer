#include "Presets.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <utility>

namespace psm {

namespace {

// Bands:             31    63   125   250   500    1k    2k    4k    8k   16k
const Preset kBuiltIns[] = {
    {"Flat",         { 0,    0,    0,    0,    0,    0,    0,    0,    0,    0}, true},
    {"Bass Boost",   { 6,    5,    4,    2,    0,    0,    0,    0,    0,    0}, true},
    {"Treble Boost", { 0,    0,    0,    0,    0,    0,    2,    4,    5,    6}, true},
    {"Vocal",        {-4,   -3,   -2,    0,    1,    2,    4,    4,    2,    0}, true},
    {"Loudness",     { 5,    4,    2,    0,   -1,   -1,    0,    2,    4,    5}, true},
    {"Music",        { 3,    3,    2,    0,   -1,   -1,    0,    1,    2,    3}, true},
    {"Game",         { 2,    3,    1,    0,   -1,    0,    2,    3,    3,    2}, true},
    {"Film",         { 4,    3,    1,    0,   -1,    1,    3,    2,    1,    1}, true},
    {"Chat",         {-12,  -8,   -4,    0,    1,    3,    4,    3,    0,   -3}, true},
    {"Podcast",      {-6,   -4,   -1,    1,    0,    1,    3,    3,    1,    0}, true},
};

bool setError(std::string* error, std::string text)
{
    if (error) *error = std::move(text);
    return false;
}

} // namespace

PresetLibrary::PresetLibrary()
    : presets_(std::begin(kBuiltIns), std::end(kBuiltIns))
{
}

const Preset* PresetLibrary::find(const std::string& name) const
{
    auto it = std::find_if(presets_.begin(), presets_.end(), [&](const Preset& p) { return p.name == name; });
    return it == presets_.end() ? nullptr : &*it;
}

Preset* PresetLibrary::findMutable(const std::string& name)
{
    return const_cast<Preset*>(std::as_const(*this).find(name));
}

bool PresetLibrary::save(const std::string& name, const EqGains& gainsDb, std::string* error)
{
    if (name.empty()) {
        return setError(error, "Preset name is empty");
    }
    if (Preset* existing = findMutable(name)) {
        if (existing->builtIn) {
            return setError(error, "\"" + name + "\" is a built-in preset; pick another name");
        }
        existing->gainsDb = gainsDb;
        return true;
    }
    presets_.push_back({name, gainsDb, false});
    return true;
}

bool PresetLibrary::rename(const std::string& from, const std::string& to, std::string* error)
{
    Preset* p = findMutable(from);
    if (!p) return setError(error, "Preset not found: " + from);
    if (p->builtIn) return setError(error, "Built-in presets cannot be renamed");
    if (to.empty()) return setError(error, "Preset name is empty");
    if (to != from && find(to)) return setError(error, "A preset named \"" + to + "\" already exists");
    p->name = to;
    return true;
}

bool PresetLibrary::remove(const std::string& name, std::string* error)
{
    const Preset* p = find(name);
    if (!p) return setError(error, "Preset not found: " + name);
    if (p->builtIn) return setError(error, "Built-in presets cannot be deleted");
    presets_.erase(presets_.begin() + (p - presets_.data()));
    return true;
}

bool PresetLibrary::loadUserPresets(const std::filesystem::path& file, std::string* error)
{
    std::ifstream in(file);
    if (!in) {
        return true; // first run: nothing saved yet
    }
    const nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
    if (j.is_discarded() || !j.contains("presets") || !j["presets"].is_array()) {
        return setError(error, "Preset file is damaged: " + file.string());
    }
    for (const auto& item : j["presets"]) {
        if (!item.contains("name") || !item["name"].is_string() || !item.contains("gains") || !item["gains"].is_array()) {
            continue;
        }
        EqGains gains{};
        const auto& arr = item["gains"];
        for (int b = 0; b < kEqBands && b < static_cast<int>(arr.size()); ++b) {
            if (arr[b].is_number()) {
                gains[b] = std::clamp(arr[b].get<float>(), kEqMinGainDb, kEqMaxGainDb);
            }
        }
        save(item["name"].get<std::string>(), gains, nullptr); // built-in name clashes are skipped
    }
    return true;
}

bool PresetLibrary::saveUserPresets(const std::filesystem::path& file, std::string* error) const
{
    nlohmann::json j;
    j["version"] = 1;
    j["presets"] = nlohmann::json::array();
    for (const Preset& p : presets_) {
        if (!p.builtIn) {
            j["presets"].push_back({{"name", p.name}, {"gains", p.gainsDb}});
        }
    }
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    // Write to a temp file then rename, so a crash never leaves a half-written preset file.
    std::filesystem::path tmp = file;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) return setError(error, "Cannot write " + tmp.string());
        out << j.dump(2);
        if (!out) return setError(error, "Cannot write " + tmp.string());
    }
    std::filesystem::rename(tmp, file, ec);
    if (ec) return setError(error, "Cannot save " + file.string() + ": " + ec.message());
    return true;
}

} // namespace psm
