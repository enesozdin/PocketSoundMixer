#include "Presets.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <utility>

namespace psm {

namespace {

constexpr PresetKind kOut = PresetKind::Output;
constexpr PresetKind kMic = PresetKind::Mic;

// Bands:                31    63   125   250   500    1k    2k    4k    8k   16k
const Preset kBuiltIns[] = {
    // App channels: shape what you listen to.
    {"Flat",         { 0,    0,    0,    0,    0,    0,    0,    0,    0,    0}, true, kOut},
    {"Bass Boost",   { 6,    5,    4,    2,    0,    0,    0,    0,    0,    0}, true, kOut},
    {"Treble Boost", { 0,    0,    0,    0,    0,    0,    2,    4,    5,    6}, true, kOut},
    {"Vocal",        {-4,   -3,   -2,    0,    1,    2,    4,    4,    2,    0}, true, kOut},
    {"Loudness",     { 5,    4,    2,    0,   -1,   -1,    0,    2,    4,    5}, true, kOut},
    {"Music",        { 3,    3,    2,    0,   -1,   -1,    0,    1,    2,    3}, true, kOut},
    {"Game",         { 2,    3,    1,    0,   -1,    0,    2,    3,    3,    2}, true, kOut},
    {"Film",         { 4,    3,    1,    0,   -1,    1,    3,    2,    1,    1}, true, kOut},
    {"Chat",         {-12,  -8,   -4,    0,    1,    3,    4,    3,    0,   -3}, true, kOut},
    {"Podcast",      {-6,   -4,   -1,    1,    0,    1,    3,    3,    1,    0}, true, kOut},
    // Mic channels: a voice lives at about 100 Hz - 8 kHz. Below that is desk thumps, hum and
    // rumble, above it hiss; 2-4 kHz carries clarity, 200-300 Hz warmth (and boom when too much).
    {"Flat",         { 0,    0,    0,    0,    0,    0,    0,    0,    0,    0}, true, kMic},
    {"Clear Voice",  {-12,  -8,   -3,   -1,    0,    1,    3,    4,    2,    0}, true, kMic},
    {"Warm Voice",   {-10,  -5,    2,    3,    1,    0,   -1,    0,   -1,   -3}, true, kMic},
    {"Broadcast",    {-12,  -8,    0,    2,   -1,   -2,    1,    4,    4,    2}, true, kMic},
    {"Cut Rumble",   {-12, -10,   -2,    0,    0,    0,    0,    0,    0,    0}, true, kMic},
    {"Less Boom",    {-8,   -6,   -4,   -4,   -2,    0,    1,    1,    0,    0}, true, kMic},
    {"Less Hiss",    { 0,    0,    0,    0,    0,    0,    0,   -2,   -7,  -10}, true, kMic},
};

constexpr const char* kKeptPreset = "Flat"; // the way back to an unchanged sound

const Preset* findBuiltIn(const std::string& name, PresetKind kind)
{
    for (const Preset& p : kBuiltIns) {
        if (p.kind == kind && p.name == name) return &p;
    }
    return nullptr;
}

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

const Preset* PresetLibrary::find(const std::string& name, PresetKind kind) const
{
    auto it = std::find_if(presets_.begin(), presets_.end(), [&](const Preset& p) { return p.kind == kind && p.name == name; });
    return it == presets_.end() ? nullptr : &*it;
}

Preset* PresetLibrary::findMutable(const std::string& name, PresetKind kind)
{
    return const_cast<Preset*>(std::as_const(*this).find(name, kind));
}

bool PresetLibrary::save(const std::string& name, PresetKind kind, const EqGains& gainsDb, std::string* error)
{
    if (name.empty()) {
        return setError(error, "Preset name is empty");
    }
    if (Preset* existing = findMutable(name, kind)) {
        if (existing->builtIn) {
            return setError(error, "\"" + name + "\" is a built-in preset; pick another name");
        }
        existing->gainsDb = gainsDb;
        return true;
    }
    presets_.push_back({name, gainsDb, false, kind});
    return true;
}

bool PresetLibrary::rename(const std::string& from, const std::string& to, PresetKind kind, std::string* error)
{
    Preset* p = findMutable(from, kind);
    if (!p) return setError(error, "Preset not found: " + from);
    if (p->builtIn) return setError(error, "Built-in presets cannot be renamed");
    if (to.empty()) return setError(error, "Preset name is empty");
    if (to != from && find(to, kind)) return setError(error, "A preset named \"" + to + "\" already exists");
    p->name = to;
    return true;
}

bool PresetLibrary::canRemove(const std::string& name, PresetKind kind) const
{
    return name != kKeptPreset && find(name, kind) != nullptr;
}

bool PresetLibrary::remove(const std::string& name, PresetKind kind, std::string* error)
{
    const Preset* p = find(name, kind);
    if (!p) return setError(error, "Preset not found: " + name);
    if (name == kKeptPreset) return setError(error, "Flat can't be deleted: it is the way back to the original sound");
    if (p->builtIn) hidden_[static_cast<int>(kind)].push_back(name);
    presets_.erase(presets_.begin() + (p - presets_.data()));
    return true;
}

void PresetLibrary::restoreBuiltIns(PresetKind kind)
{
    // Built-ins first in their original order, then the user's presets as they were.
    std::vector<Preset> result;
    for (const Preset& b : kBuiltIns) {
        const Preset* current = find(b.name, b.kind);
        if (current && current->builtIn) result.push_back(*current);
        else if (!current && b.kind == kind) result.push_back(b); // was deleted: comes back
        // else a user preset took the name meanwhile (the user's one wins), or the other kind stays hidden
    }
    for (const Preset& p : presets_) {
        if (!p.builtIn) result.push_back(p);
    }
    presets_ = std::move(result);
    hidden_[static_cast<int>(kind)].clear();
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
    // Files from before the mic presets have only output presets, with no "kind".
    for (const auto& [key, kind] : {std::pair{"hiddenBuiltIns", kOut}, std::pair{"hiddenMicBuiltIns", kMic}}) {
        if (!j.contains(key) || !j[key].is_array()) continue;
        for (const auto& h : j[key]) {
            if (h.is_string() && h.get<std::string>() != kKeptPreset && findBuiltIn(h.get<std::string>(), kind)) {
                remove(h.get<std::string>(), kind, nullptr);
            }
        }
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
        const PresetKind kind = item.value("kind", std::string()) == "mic" ? kMic : kOut;
        save(item["name"].get<std::string>(), kind, gains, nullptr); // built-in name clashes are skipped
    }
    return true;
}

bool PresetLibrary::saveUserPresets(const std::filesystem::path& file, std::string* error) const
{
    nlohmann::json j;
    j["version"] = 1;
    j["hiddenBuiltIns"] = hidden_[static_cast<int>(kOut)];
    j["hiddenMicBuiltIns"] = hidden_[static_cast<int>(kMic)];
    j["presets"] = nlohmann::json::array();
    for (const Preset& p : presets_) {
        if (!p.builtIn) {
            j["presets"].push_back({{"name", p.name}, {"kind", p.kind == kMic ? "mic" : "output"}, {"gains", p.gainsDb}});
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
