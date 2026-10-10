#include "Session.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>

namespace psm {

namespace {
constexpr int kSessionVersion = 3;
}

SessionConfig defaultSession()
{
    SessionConfig s;
    for (const char* name : {"Music", "Game", "Film", "Chat", "Podcast"}) {
        ChannelConfig c;
        c.name = name;
        c.preset = name;
        s.channels.push_back(c);
    }
    // Muted so nobody hears themselves through the speakers; the meter still shows the mic works.
    ChannelConfig mic;
    mic.name = "Mic";
    mic.kind = "mic";
    mic.preset = "Clear Voice";
    mic.mute = true;
    mic.sources.push_back({"input", {}, {}});
    s.channels.push_back(mic);
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
    if (j.value("version", 1) < kSessionVersion) {
        return false; // layouts from test builds before v3: start with the six default channels
    }
    SessionConfig s;
    s.masterVolume = std::clamp(j.value("master", 1.0f), 0.0f, 1.0f);
    s.outputDevice = j.value("outputDevice", std::string());
    s.theme = j.value("theme", std::string("Dark"));
    s.language = j.value("language", std::string("en"));
    s.appAutoRoute = j.value("appAutoRoute", true);
    s.appSilentOutput = j.value("appSilentOutput", std::string());
    s.trayEnabled = j.value("trayEnabled", true);
    s.startWithWindows = j.value("startWithWindows", true);
    for (const auto& c : j["channels"]) {
        if (!c.is_object()) continue;
        ChannelConfig cc;
        cc.name = c.value("name", std::string("Channel"));
        cc.kind = c.value("kind", std::string());
        cc.preset = c.value("preset", std::string("Flat"));
        if (c.contains("gains") && c["gains"].is_array()) {
            for (int b = 0; b < kEqBands && b < static_cast<int>(c["gains"].size()); ++b) {
                if (c["gains"][b].is_number()) {
                    cc.gainsDb[b] = std::clamp(c["gains"][b].get<float>(), kEqMinGainDb, kEqMaxGainDb);
                }
            }
        }
        cc.volume = std::clamp(c.value("volume", 1.0f), 0.0f, 1.0f);
        cc.pan = std::clamp(c.value("pan", 0.0f), -1.0f, 1.0f);
        cc.mute = c.value("mute", false);
        cc.solo = c.value("solo", false);
        cc.output = c.value("output", std::string());
        if (c.contains("sources") && c["sources"].is_array()) {
            for (const auto& src : c["sources"]) {
                if (!src.is_object()) continue;
                SourceConfig sc;
                sc.type = src.value("type", std::string());
                sc.appExe = src.value("app", std::string());
                sc.inputDevice = src.value("input", std::string());
                if (sc.type == "app" ? !sc.appExe.empty() : sc.type == "input") cc.sources.push_back(std::move(sc));
            }
        }
        if (cc.kind != "apps" && cc.kind != "mic") { // kind missing: a channel holding a mic is the mic channel
            const bool hasInput = std::any_of(cc.sources.begin(), cc.sources.end(), [](const SourceConfig& x) { return x.type == "input"; });
            cc.kind = hasInput ? "mic" : "apps";
        }
        // A mic channel holds one microphone; an app channel holds apps only.
        const std::string keep = cc.kind == "mic" ? "input" : "app";
        cc.sources.erase(std::remove_if(cc.sources.begin(), cc.sources.end(), [&](const SourceConfig& x) { return x.type != keep; }),
                         cc.sources.end());
        if (cc.kind == "mic" && cc.sources.size() > 1) cc.sources.resize(1);
        s.channels.push_back(std::move(cc));
    }
    out = std::move(s);
    return true;
}

bool saveSession(const std::filesystem::path& file, const SessionConfig& session, std::string* error)
{
    nlohmann::json j;
    j["version"] = kSessionVersion;
    j["master"] = session.masterVolume;
    j["outputDevice"] = session.outputDevice;
    j["theme"] = session.theme;
    j["language"] = session.language;
    j["appAutoRoute"] = session.appAutoRoute;
    j["appSilentOutput"] = session.appSilentOutput;
    j["trayEnabled"] = session.trayEnabled;
    j["startWithWindows"] = session.startWithWindows;
    j["channels"] = nlohmann::json::array();
    for (const ChannelConfig& c : session.channels) {
        nlohmann::json sources = nlohmann::json::array();
        for (const SourceConfig& src : c.sources) {
            sources.push_back(src.type == "app" ? nlohmann::json{{"type", "app"}, {"app", src.appExe}}
                                                : nlohmann::json{{"type", "input"}, {"input", src.inputDevice}});
        }
        j["channels"].push_back({
            {"name", c.name}, {"kind", c.kind}, {"preset", c.preset}, {"gains", c.gainsDb},
            {"volume", c.volume}, {"pan", c.pan}, {"mute", c.mute}, {"solo", c.solo}, {"output", c.output},
            {"sources", std::move(sources)},
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
