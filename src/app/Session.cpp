#include "Session.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>

namespace psm {

SessionConfig defaultSession()
{
    SessionConfig s;
    for (const char* name : {"Music", "Game", "Film", "Chat", "Podcast"}) {
        ChannelConfig c;
        c.name = name;
        c.preset = name;
        s.channels.push_back(c);
    }
    return s;
}

bool loadSession(const std::filesystem::path& file, SessionConfig& out, std::string* error)
{
    std::ifstream in(file);
    if (!in) {
        return false; // no session yet
    }
    const nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
    if (j.is_discarded() || !j.contains("channels") || !j["channels"].is_array()) {
        if (error) *error = "Session file is damaged, starting fresh: " + file.string();
        return false;
    }
    SessionConfig s;
    s.masterVolume = std::clamp(j.value("master", 1.0f), 0.0f, 2.0f);
    for (const auto& c : j["channels"]) {
        if (!c.is_object()) continue;
        ChannelConfig cc;
        cc.name = c.value("name", std::string("Channel"));
        cc.preset = c.value("preset", std::string("Flat"));
        if (c.contains("gains") && c["gains"].is_array()) {
            for (int b = 0; b < kEqBands && b < static_cast<int>(c["gains"].size()); ++b) {
                if (c["gains"][b].is_number()) {
                    cc.gainsDb[b] = std::clamp(c["gains"][b].get<float>(), kEqMinGainDb, kEqMaxGainDb);
                }
            }
        }
        cc.volume = std::clamp(c.value("volume", 1.0f), 0.0f, 2.0f);
        cc.pan = std::clamp(c.value("pan", 0.0f), -1.0f, 1.0f);
        cc.mute = c.value("mute", false);
        cc.solo = c.value("solo", false);
        cc.sourceType = c.value("source", std::string("none"));
        cc.filePath = c.value("file", std::string());
        cc.loop = c.value("loop", true);
        cc.inputDevice = c.value("input", std::string());
        s.channels.push_back(std::move(cc));
    }
    out = std::move(s);
    return true;
}

bool saveSession(const std::filesystem::path& file, const SessionConfig& session, std::string* error)
{
    nlohmann::json j;
    j["version"] = 1;
    j["master"] = session.masterVolume;
    j["channels"] = nlohmann::json::array();
    for (const ChannelConfig& c : session.channels) {
        j["channels"].push_back({
            {"name", c.name}, {"preset", c.preset}, {"gains", c.gainsDb},
            {"volume", c.volume}, {"pan", c.pan}, {"mute", c.mute}, {"solo", c.solo},
            {"source", c.sourceType}, {"file", c.filePath}, {"loop", c.loop}, {"input", c.inputDevice},
        });
    }
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::filesystem::path tmp = file;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        out << j.dump(2);
        if (!out) {
            if (error) *error = "Cannot write " + tmp.string();
            return false;
        }
    }
    std::filesystem::rename(tmp, file, ec);
    if (ec) {
        if (error) *error = "Cannot save session: " + ec.message();
        return false;
    }
    return true;
}

} // namespace psm
