#include "MixerUI.h"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace psm {

namespace {

constexpr float kMeterFallDbPerSec = 20.0f;
constexpr float kMeterHoldSeconds = 1.5f;
constexpr float kMeterFloorDb = -70.0f;
constexpr float kStripWidth = 326.0f;
constexpr float kSliderHeight = 160.0f;
constexpr float kBandWidth = 22.0f;
constexpr int kSourceRows = 3;
const char* const kDefaultMicName = "Default microphone";

// Volume is shown as 0..100 %. The curve is squared so the fader feels even to the ear:
// 50 % is about -12 dB (clearly quieter), 10 % about -40 dB (barely audible).
float percentToGain(float pct)
{
    const float x = std::clamp(pct, 0.0f, 100.0f) * 0.01f;
    return x * x;
}

float gainToPercent(float gain)
{
    return std::sqrt(std::clamp(gain, 0.0f, 1.0f)) * 100.0f;
}

// IEC 60268-18 meter scale: the same non-linear scale as broadcast and DAW meters.
// Normal music peaks (-12..-3 dBFS) fill 70..95 % of the bar instead of hugging the middle.
float meterDeflection(float db)
{
    if (db < -70.0f) return 0.0f;
    if (db < -60.0f) return (db + 70.0f) * 0.0025f;
    if (db < -50.0f) return (db + 60.0f) * 0.005f + 0.025f;
    if (db < -40.0f) return (db + 50.0f) * 0.0075f + 0.075f;
    if (db < -30.0f) return (db + 40.0f) * 0.015f + 0.15f;
    if (db < -20.0f) return (db + 30.0f) * 0.02f + 0.3f;
    if (db < 0.0f) return (db + 20.0f) * 0.025f + 0.5f;
    return 1.0f;
}

template <size_t N>
void copyToBuf(std::array<char, N>& buf, const std::string& s)
{
    const size_t n = std::min(s.size(), N - 1);
    std::memcpy(buf.data(), s.data(), n);
    buf[n] = '\0';
}

bool equalsNoCase(const std::string& a, const std::string& b)
{
    return a.size() == b.size()
        && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

// "Spotify.exe" -> "Spotify"
std::string appDisplayName(const std::string& exe)
{
    const size_t n = exe.size();
    if (n > 4 && equalsNoCase(exe.substr(n - 4), ".exe")) return exe.substr(0, n - 4);
    return exe;
}

bool toggleButton(const char* label, bool on, ImVec4 onColor, ImVec2 size)
{
    if (on) {
        ImGui::PushStyleColor(ImGuiCol_Button, onColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, onColor);
    }
    const bool pressed = ImGui::Button(label, size);
    if (on) {
        ImGui::PopStyleColor(2);
    }
    return pressed;
}

} // namespace

// Jumps up instantly, holds the peak line briefly, then falls smoothly. Returns true while moving.
static bool updateMeter(float& db, float& holdDb, float& holdSeconds, float peakLinear, float dt)
{
    const float peakDb = peakLinear > 0.0f ? 20.0f * std::log10(peakLinear) : -90.0f;
    db = std::max({peakDb, db - kMeterFallDbPerSec * dt, -90.0f});
    if (peakDb >= holdDb) {
        holdDb = peakDb;
        holdSeconds = kMeterHoldSeconds;
    } else if ((holdSeconds -= dt) <= 0.0f) {
        holdDb = db;
    }
    return db > kMeterFloorDb || holdDb > kMeterFloorDb;
}

static void drawMeterBar(ImDrawList* dl, ImVec2 min, ImVec2 max, float db, float holdDb, bool vertical, bool dimmed)
{
    const ThemeColors& tc = themeColors();
    dl->AddRectFilled(min, max, tc.meterBack);
    const ImU32 alpha = dimmed ? 110u : 255u; // muted channel: still shows the signal, greyed out
    const auto colorFor = [&tc, alpha](float d) {
        const ImU32 c = d > -1.0f ? tc.meterLimit : d > -9.0f ? tc.meterLoud : tc.meterNormal;
        return (c & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
    };
    const float t = meterDeflection(db);
    const float th = meterDeflection(holdDb);
    if (vertical) {
        const float h = max.y - min.y;
        if (t > 0.0f) dl->AddRectFilled(ImVec2(min.x, max.y - h * t), max, colorFor(db));
        if (th > 0.0f) {
            const float y = max.y - h * th;
            dl->AddRectFilled(ImVec2(min.x, y), ImVec2(max.x, y + 2.0f), colorFor(holdDb));
        }
    } else {
        const float w = max.x - min.x;
        if (t > 0.0f) dl->AddRectFilled(min, ImVec2(min.x + w * t, max.y), colorFor(db));
        if (th > 0.0f) {
            const float x = min.x + w * th;
            dl->AddRectFilled(ImVec2(x - 2.0f, min.y), ImVec2(x, max.y), colorFor(holdDb));
        }
    }
}

MixerUI::MixerUI(AudioEngine& engine, PresetLibrary& presets, std::filesystem::path presetFile)
    : engine_(engine)
    , presets_(presets)
    , presetFile_(std::move(presetFile))
{
}

// ---------------------------------------------------------------------------------------------
// Session

void MixerUI::applySession(const SessionConfig& session)
{
    while (!strips_.empty()) {
        removeStrip(strips_.size() - 1);
    }
    engine_.mixer().masterVolume = session.masterVolume;
    appAutoRoute_ = session.appAutoRoute;
    appSilentOutput_ = session.appSilentOutput;
    masterVolumePct_ = gainToPercent(session.masterVolume);
    setTheme(themeFromName(session.theme));

    for (const ChannelConfig& c : session.channels) {
        Strip* s = addStrip(c.name, c.kind == "mic");
        if (!s) break;
        Channel& ch = *s->channel;
        // A saved custom curve wins over the preset, which may have been edited or deleted since.
        const Preset* p = presets_.find(c.preset);
        const bool allZero = std::all_of(c.gainsDb.begin(), c.gainsDb.end(), [](float g) { return g == 0.0f; });
        if (p && (allZero || p->gainsDb == c.gainsDb)) {
            applyPreset(*s, *p);
        } else {
            ch.setGains(c.gainsDb);
            s->preset = p ? c.preset : "Custom";
            s->presetModified = p != nullptr;
            s->curveDirty = true;
        }
        ch.volume = c.volume;
        s->volumePct = gainToPercent(c.volume);
        ch.pan = c.pan;
        ch.mute = c.mute;
        ch.solo = c.solo;
        for (const SourceConfig& src : c.sources) {
            if (src.type == "input") {
                addInput(*s, src.inputDevice);
            } else if (src.type == "app") {
                addApp(*s, src.appExe);
            }
        }
    }
}

SessionConfig MixerUI::captureSession() const
{
    SessionConfig session;
    session.masterVolume = engine_.mixer().masterVolume.load();
    session.outputDevice = engine_.requestedOutput();
    session.appAutoRoute = appAutoRoute_;
    session.appSilentOutput = appSilentOutput_;
    session.theme = themeName(theme_);
    for (const Strip& s : strips_) {
        const Channel& ch = *s.channel;
        ChannelConfig c;
        c.name = ch.name();
        c.kind = s.isMic ? "mic" : "apps";
        c.preset = s.preset;
        c.gainsDb = ch.gains();
        c.volume = ch.volume.load();
        c.pan = ch.pan.load();
        c.mute = ch.mute.load();
        c.solo = ch.solo.load();
        for (int i = 0; i < Channel::kMaxSources; ++i) {
            const AudioSource* src = ch.source(i);
            if (!src) continue;
            if (src->kind() == SourceKind::Input) {
                const std::string& name = static_cast<const InputSource*>(src)->deviceName();
                c.sources.push_back({"input", {}, name == kDefaultMicName ? std::string() : name});
            } else if (src->kind() == SourceKind::App) {
                c.sources.push_back({"app", static_cast<const AppSource*>(src)->exeName(), {}});
            }
        }
        session.channels.push_back(std::move(c));
    }
    return session;
}

// ---------------------------------------------------------------------------------------------
// Actions

MixerUI::Strip* MixerUI::addStrip(const std::string& name, bool isMic)
{
    Channel* ch = engine_.mixer().addChannel(name);
    if (!ch) {
        status_ = "Channel limit reached (" + std::to_string(Mixer::kMaxChannels) + ")";
        return nullptr;
    }
    Strip s;
    s.channel = ch;
    s.isMic = isMic;
    copyToBuf(s.nameBuf, name);
    strips_.push_back(s);
    return &strips_.back();
}

void MixerUI::removeStrip(size_t index)
{
    engine_.mixer().removeChannel(strips_[index].channel);
    strips_.erase(strips_.begin() + static_cast<std::ptrdiff_t>(index));
}

void MixerUI::addInput(Strip& strip, const std::string& deviceName)
{
    engine_.mixer().clearSources(strip.channel); // a mic channel holds one microphone
    std::string err;
    std::unique_ptr<InputSource> src = engine_.openInput(deviceName, &err);
    if (!src) {
        strip.error = err;
        return;
    }
    engine_.mixer().addSource(strip.channel, std::move(src));
    strip.error.clear();
}

MixerUI::Strip* MixerUI::findAppStrip(const std::string& exeName, int* slot)
{
    for (Strip& s : strips_) {
        for (int i = 0; i < Channel::kMaxSources; ++i) {
            const AudioSource* src = s.channel->source(i);
            if (src && src->kind() == SourceKind::App && equalsNoCase(static_cast<const AppSource*>(src)->exeName(), exeName)) {
                *slot = i;
                return &s;
            }
        }
    }
    return nullptr;
}

void MixerUI::addApp(Strip& strip, const std::string& exeName)
{
    // An app lives in one channel only; picking it again moves it here.
    int oldSlot = -1;
    if (Strip* owner = findAppStrip(exeName, &oldSlot)) {
        if (owner == &strip) return;
        engine_.mixer().replaceSource(owner->channel, oldSlot, nullptr);
        // The old capture must put the app's output back before the new one reads it,
        // or the new one would remember the parked device as the app's own setting.
        engine_.mixer().flushGarbage(200);
    }
    if (strip.channel->freeSourceSlot() < 0) {
        strip.error = "This channel is full (" + std::to_string(Channel::kMaxSources) + " sources)";
        return;
    }
    std::string err;
    std::unique_ptr<AppSource> src = engine_.openApp(exeName, silentOutputId(), &err);
    if (!src) {
        strip.error = err;
        return;
    }
    engine_.mixer().addSource(strip.channel, std::move(src));
    strip.error.clear();
}

void MixerUI::setTheme(Theme theme)
{
    theme_ = theme;
    applyTheme(theme, uiScale_);
}

void MixerUI::resetChannels()
{
    // Back to the six default channels; the Master output, volume and theme stay as they are.
    SessionConfig fresh = defaultSession();
    const SessionConfig current = captureSession();
    fresh.masterVolume = current.masterVolume;
    fresh.outputDevice = current.outputDevice;
    fresh.theme = current.theme;
    fresh.appAutoRoute = current.appAutoRoute;
    fresh.appSilentOutput = current.appSilentOutput;
    while (!strips_.empty()) {
        removeStrip(strips_.size() - 1);
    }
    engine_.mixer().flushGarbage(200); // removed apps get their own output back before anything new starts
    applySession(fresh);
}

void MixerUI::removeSource(Strip& strip, int slot)
{
    engine_.mixer().replaceSource(strip.channel, slot, nullptr);
}

std::string MixerUI::silentOutputId()
{
    if (!appAutoRoute_) return {};
    if (outputDevices_.empty()) outputDevices_ = listOutputDevices();
    const std::string& mixerOutput = engine_.outputDeviceName();
    if (!appSilentOutput_.empty()) {
        for (const OutputDeviceInfo& d : outputDevices_) {
            if (d.name == appSilentOutput_ && !d.isDefault && d.name != mixerOutput) return d.id;
        }
    }
    return pickSilentOutputId(outputDevices_, mixerOutput);
}

void MixerUI::reopenAppChannels()
{
    // Routing settings changed: restart app captures so they pick up the new setting.
    // Destroying the old source restores the app's output first.
    for (Strip& s : strips_) {
        for (int i = 0; i < Channel::kMaxSources; ++i) {
            const AudioSource* src = s.channel->source(i);
            if (!src || src->kind() != SourceKind::App) continue;
            const std::string exe = static_cast<const AppSource*>(src)->exeName();
            engine_.mixer().replaceSource(s.channel, i, nullptr);
            engine_.mixer().flushGarbage(200); // see addApp()
            std::string err;
            if (auto fresh = engine_.openApp(exe, silentOutputId(), &err)) {
                engine_.mixer().replaceSource(s.channel, i, std::move(fresh));
            } else {
                s.error = err;
            }
        }
    }
}

void MixerUI::selectOutput(const std::string& name)
{
    std::string err;
    engine_.setOutputDevice(name, &err);
    status_ = err;
    // The spare output for parked apps must never be the one the mixer now plays on.
    outputDevices_.clear();
    reopenAppChannels();
}

void MixerUI::drawAppRoutingSettings()
{
    if (ImGui::Checkbox("Hear apps only through the mixer", &appAutoRoute_)) {
        reopenAppChannels();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Moves the app's own sound to a spare output while it is in a channel,\n"
                          "so you don't hear it twice. Put back when you remove it from the channel.");
    }
    if (!appAutoRoute_) {
        if (ImGui::Button("Open Windows sound settings")) openAppVolumeSettings();
        return;
    }
    const std::string current = silentOutputId();
    if (current.empty()) {
        ImGui::PushTextWrapPos(380.0f);
        ImGui::TextColored(themeColors().warningText,
                           "No spare output device, so captured apps also play directly. "
                           "Connect a second output (a monitor with audio, a USB headset) or install the free VB-Cable driver.");
        ImGui::PopTextWrapPos();
        return;
    }
    std::string currentName;
    for (const OutputDeviceInfo& d : outputDevices_) {
        if (d.id == current) currentName = d.name;
    }
    ImGui::TextDisabled("Spare output for app sound:");
    ImGui::SetNextItemWidth(320.0f);
    const std::string label = appSilentOutput_.empty() ? "Automatic (" + currentName + ")" : currentName;
    if (ImGui::BeginCombo("##silent", label.c_str())) {
        if (ImGui::Selectable("Automatic", appSilentOutput_.empty())) {
            appSilentOutput_.clear();
            reopenAppChannels();
        }
        for (const OutputDeviceInfo& d : outputDevices_) {
            if (d.isDefault || d.name == engine_.outputDeviceName()) continue; // you listen there
            if (ImGui::Selectable(d.name.c_str(), d.name == appSilentOutput_)) {
                appSilentOutput_ = d.name;
                reopenAppChannels();
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::SmallButton("Reset all app outputs")) {
        resetAllAppOutputs();
        reopenAppChannels();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Puts every app back on your normal output, like Windows' own Reset button.");
    }
}

void MixerUI::applyPreset(Strip& strip, const Preset& preset)
{
    strip.channel->setGains(preset.gainsDb);
    strip.preset = preset.name;
    strip.presetModified = false;
    strip.curveDirty = true;
}

void MixerUI::updateCurve(Strip& strip)
{
    // Only recomputed after an edit, never per frame.
    GraphicEq eq;
    eq.prepare(static_cast<float>(engine_.sampleRate()));
    eq.setGains(strip.channel->gains());
    for (int i = 0; i < kCurvePoints; ++i) {
        const float f = 20.0f * std::pow(1000.0f, static_cast<float>(i) / (kCurvePoints - 1)); // 20 Hz .. 20 kHz, log
        strip.curve[i] = std::clamp(eq.responseDb(f), -18.0f, 18.0f);
    }
    strip.curveDirty = false;
}

void MixerUI::savePresets()
{
    std::string err;
    if (!presets_.saveUserPresets(presetFile_, &err)) {
        presetError_ = err;
    }
}


// ---------------------------------------------------------------------------------------------
// Drawing

void MixerUI::draw(float dt)
{
    animating_ = false;
    engine_.mixer().collectGarbage();

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("PocketSoundMixer", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
                     | ImGuiWindowFlags_NoBringToFrontOnFocus);

    drawMasterBar(dt);
    ImGui::Separator();
    drawToolBar();

    // Strips wrap into rows that fill the window width; extra rows scroll vertically,
    // so every channel stays visible without a sideways scrollbar.
    ImGui::BeginChild("strips", ImVec2(0, 0), ImGuiChildFlags_None);
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const int perRow = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + spacing) / (kStripWidth + spacing)));
    for (size_t i = 0; i < strips_.size(); ++i) {
        if (i % static_cast<size_t>(perRow) != 0) ImGui::SameLine();
        drawStrip(strips_[i], i, dt);
    }
    if (scrollToNewStrip_) {
        ImGui::SetScrollHereY(1.0f);
        scrollToNewStrip_ = false;
    }
    if (strips_.empty()) {
        ImGui::TextDisabled("No channels. Click \"+ Add channel\".");
    }
    ImGui::EndChild();

    if (pendingRemove_ >= 0 && pendingRemove_ < static_cast<int>(strips_.size())) {
        removeStrip(static_cast<size_t>(pendingRemove_));
    }
    pendingRemove_ = -1;

    ImGui::End();

    if (showPresetManager_) drawPresetManager();
    if (showHelp_) drawHelp();
}

void MixerUI::drawMasterBar(float dt)
{
    Mixer& mixer = engine_.mixer();

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Master");
    ImGui::SameLine();

    // Where everything you hear comes out. "System default" follows Windows' default device.
    const std::string& requested = engine_.requestedOutput();
    const std::string label = requested.empty() ? "System default (" + engine_.outputDeviceName() + ")" : engine_.outputDeviceName();
    ImGui::SetNextItemWidth(300.0f);
    if (ImGui::BeginCombo("##output", label.c_str())) {
        if (ImGui::IsWindowAppearing()) playbackDevices_ = engine_.outputDeviceNames();
        if (ImGui::Selectable("System default", requested.empty())) {
            selectOutput({});
        }
        for (const std::string& name : playbackDevices_) {
            if (ImGui::Selectable(name.c_str(), name == requested)) {
                selectOutput(name);
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Speakers or headphones the mixer plays on");

    ImGui::SameLine();
    ImGui::SetNextItemWidth(200.0f);
    // NoInput: a double-click must not turn the fader into a text box.
    if (ImGui::SliderFloat("##master", &masterVolumePct_, 0.0f, 100.0f, "Volume %.0f%%", ImGuiSliderFlags_NoInput)) {
        mixer.masterVolume = percentToGain(masterVolumePct_);
    }

    ImGui::SameLine();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetFrameHeight();
    for (int side = 0; side < 2; ++side) {
        Meter& m = masterMeter_[side];
        animating_ |= updateMeter(m.db, m.holdDb, m.holdSeconds, mixer.takeMasterPeak(side), dt);
        const float y0 = p.y + side * (h * 0.5f);
        drawMeterBar(ImGui::GetWindowDrawList(), ImVec2(p.x, y0 + 1), ImVec2(p.x + 240.0f, y0 + h * 0.5f - 1), m.db, m.holdDb, false, false);
    }
    ImGui::Dummy(ImVec2(240.0f, h));
    ImGui::SameLine();
    ImGui::TextDisabled("%u Hz", engine_.sampleRate());
}

void MixerUI::drawToolBar()
{
    if (ImGui::Button("+ Add channel")) {
        if (addStrip("Channel " + std::to_string(strips_.size() + 1), false)) {
            scrollToNewStrip_ = true;
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("A new channel for apps");
    ImGui::SameLine();
    if (ImGui::Button("+ Add mic channel")) {
        if (Strip* s = addStrip("Mic", true)) {
            s->channel->mute = true; // never surprise anyone with their own voice on the speakers
            addInput(*s, "");
            scrollToNewStrip_ = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Presets")) showPresetManager_ = !showPresetManager_;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::BeginCombo("##theme", themeName(theme_))) {
        for (Theme t : {Theme::Dark, Theme::Midnight, Theme::Light}) {
            if (ImGui::Selectable(themeName(t), t == theme_)) setTheme(t);
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Theme");
    ImGui::SameLine();
    if (ImGui::Button("Reset channels")) ImGui::OpenPopup("reset");
    if (ImGui::BeginPopup("reset")) {
        ImGui::TextUnformatted("Replace all channels with the six default ones?");
        ImGui::TextDisabled("Music, Game, Film, Chat, Podcast and Mic. Your apps go back to normal.");
        if (ImGui::Button("Reset")) {
            resetChannels();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Help")) showHelp_ = !showHelp_;
    if (!status_.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(themeColors().errorText, "%s", status_.c_str());
    }
}

void MixerUI::drawStrip(Strip& strip, size_t index, float dt)
{
    Channel& ch = *strip.channel;
    ImGui::PushID(strip.channel);
    ImGui::BeginChild("strip", ImVec2(kStripWidth, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);

    // Name + remove.
    ImGui::SetNextItemWidth(-ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x);
    if (ImGui::InputText("##name", strip.nameBuf.data(), strip.nameBuf.size())) {
        ch.setName(strip.nameBuf.data());
    }
    ImGui::SameLine();
    if (ImGui::Button("x", ImVec2(ImGui::GetFrameHeight(), 0))) {
        ImGui::OpenPopup("remove");
    }
    if (ImGui::BeginPopup("remove")) {
        ImGui::Text("Remove \"%s\"?", ch.name().c_str());
        if (ImGui::Button("Remove")) {
            pendingRemove_ = static_cast<int>(index);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (strip.isMic) drawMicSource(strip);
    else drawAppSources(strip);
    drawPresetRow(strip);
    if (strip.curveDirty) {
        updateCurve(strip);
    }
    ImGui::PlotLines("##curve", strip.curve.data(), kCurvePoints, 0, nullptr, -18.0f, 18.0f, ImVec2(-1.0f, 44.0f));

    drawEq(strip);

    // Volume fader + meters on the right of the EQ.
    ImGui::SameLine();
    ImGui::BeginGroup();
    if (ImGui::VSliderFloat("##vol", ImVec2(30.0f, kSliderHeight), &strip.volumePct, 0.0f, 100.0f, "", ImGuiSliderFlags_NoInput)) {
        ch.volume = percentToGain(strip.volumePct);
    }
    if (ImGui::IsItemActive() || ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Volume %.0f%%", strip.volumePct);
    }
    ImGui::Text("%3.0f%%", strip.volumePct);
    ImGui::EndGroup();

    ImGui::SameLine();
    const bool muted = ch.mute.load();
    const ImVec2 mp = ImGui::GetCursorScreenPos();
    for (int side = 0; side < 2; ++side) {
        Meter& m = strip.meter[side];
        animating_ |= updateMeter(m.db, m.holdDb, m.holdSeconds, ch.takePeak(side), dt);
        const float x0 = mp.x + side * 8.0f;
        drawMeterBar(ImGui::GetWindowDrawList(), ImVec2(x0, mp.y), ImVec2(x0 + 6.0f, mp.y + kSliderHeight), m.db, m.holdDb, true, muted);
    }
    ImGui::Dummy(ImVec2(14.0f, kSliderHeight));

    // Pan, mute, solo.
    float pan = ch.pan.load();
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::SliderFloat("##pan", &pan, -1.0f, 1.0f, pan == 0.0f ? "Center" : (pan < 0 ? "Left %.2f" : "Right %.2f"),
                           ImGuiSliderFlags_NoInput)) {
        ch.pan = std::fabs(pan) < 0.05f ? 0.0f : pan; // snaps to center, so it is easy to hit
    }
    ImGui::SameLine();
    if (toggleButton("M", muted, themeColors().muteOn, ImVec2(32, 0))) ch.mute = !muted;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Mute");
    ImGui::SameLine();
    const bool soloed = ch.solo.load();
    if (toggleButton("S", soloed, themeColors().soloOn, ImVec2(32, 0))) ch.solo = !soloed;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Solo: only soloed channels play");

    if (!strip.error.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(themeColors().errorText, "%s", strip.error.c_str());
        ImGui::PopTextWrapPos();
    }

    ImGui::EndChild();
    ImGui::PopID();
}

void MixerUI::drawAppSources(Strip& strip)
{
    Channel& ch = *strip.channel;

    // Fixed-height list so every strip's EQ lines up; more than three apps scroll.
    const float rowH = ImGui::GetFrameHeightWithSpacing();
    ImGui::BeginChild("sources", ImVec2(-1.0f, rowH * kSourceRows), ImGuiChildFlags_None);
    int shown = 0;
    for (int i = 0; i < Channel::kMaxSources; ++i) {
        AudioSource* src = ch.source(i);
        if (!src || src->kind() != SourceKind::App) continue;
        ++shown;
        ImGui::PushID(i);
        if (ImGui::SmallButton("x")) {
            removeSource(strip, i);
            ImGui::PopID();
            continue;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove from this channel");
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        const auto* app = static_cast<AppSource*>(src);
        const std::string name = appDisplayName(app->exeName());
        switch (app->state()) {
        case AppSource::State::Capturing:
            ImGui::Text("%s", name.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled(app->isRerouted() ? "(only via mixer)" : "(playing)");
            break;
        case AppSource::State::WaitingForApp:
            ImGui::Text("%s", name.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("(waiting for it to start)");
            break;
        case AppSource::State::Failed:
            ImGui::TextColored(themeColors().errorText, "%s: %s", name.c_str(), app->lastError().c_str());
            break;
        }
        ImGui::PopID();
    }
    if (shown == 0) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled(appCaptureSupported() ? "No apps yet. Click \"+ App\" to add one or more."
                                                  : "Putting apps in channels works on Windows only for now.");
        ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();

    ImGui::BeginDisabled(!appCaptureSupported());
    if (ImGui::Button("+ App")) {
        audioApps_ = listAudioApps();
        outputDevices_ = listOutputDevices();
        appExeBuf_[0] = '\0';
        ImGui::OpenPopup("app");
    }
    ImGui::EndDisabled();
    if (shown > 1) {
        ImGui::SameLine();
        if (ImGui::Button("Clear all")) {
            engine_.mixer().clearSources(&ch);
        }
    }

    if (ImGui::BeginPopup("app")) {
        ImGui::TextUnformatted("Apps playing sound now (pick as many as you like):");
        for (const AudioAppInfo& app : audioApps_) {
            int slot = -1;
            const Strip* owner = findAppStrip(app.exeName, &slot);
            std::string label = app.displayName;
            if (owner == &strip) label += "  (in this channel)";
            else if (owner) label += "  (in " + owner->channel->name() + ", moves here)";
            if (ImGui::Selectable(label.c_str(), owner == &strip, ImGuiSelectableFlags_NoAutoClosePopups)) {
                addApp(strip, app.exeName);
            }
        }
        if (audioApps_.empty()) {
            ImGui::TextDisabled("None right now. Start playing something, or type the exe name:");
        }
        ImGui::SetNextItemWidth(220.0f);
        const bool enter = ImGui::InputTextWithHint("##exe", "e.g. Spotify.exe", appExeBuf_.data(), appExeBuf_.size(),
                                                    ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if ((ImGui::Button("Add") || enter) && appExeBuf_[0] != '\0') {
            addApp(strip, appExeBuf_.data());
            appExeBuf_[0] = '\0';
        }
        ImGui::Separator();
        drawAppRoutingSettings();
        ImGui::EndPopup();
    }
}

void MixerUI::drawMicSource(Strip& strip)
{
    Channel& ch = *strip.channel;
    const float rowH = ImGui::GetFrameHeightWithSpacing();
    ImGui::BeginChild("sources", ImVec2(-1.0f, rowH * kSourceRows), ImGuiChildFlags_None);

    const AudioSource* src = ch.source(0);
    for (int i = 1; !src && i < Channel::kMaxSources; ++i) src = ch.source(i);
    const std::string current = src && src->kind() == SourceKind::Input
                              ? static_cast<const InputSource*>(src)->deviceName()
                              : std::string("None");
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Microphone");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##mic", current.c_str())) {
        if (ImGui::IsWindowAppearing()) captureDevices_ = engine_.captureDeviceNames();
        if (ImGui::Selectable(kDefaultMicName, current == kDefaultMicName)) addInput(strip, "");
        for (const std::string& name : captureDevices_) {
            if (ImGui::Selectable(name.c_str(), name == current)) addInput(strip, name);
        }
        if (captureDevices_.empty()) ImGui::TextDisabled("No microphones found");
        ImGui::Separator();
        if (ImGui::Selectable("None", src == nullptr)) engine_.mixer().clearSources(&ch);
        ImGui::EndCombo();
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled(ch.mute.load() ? "Muted: the bar shows your mic works. Unmute (M) to hear yourself; use headphones."
                                       : "You hear yourself now. Use headphones, or mute (M) to stop the echo.");
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
    ImGui::Dummy(ImVec2(0.0f, ImGui::GetFrameHeight())); // lines up with the "+ App" row of app channels
}

void MixerUI::drawHelp()
{
    // Docked to the right edge and sized from the main window every frame, so it follows resizes.
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float width = std::min(vp->WorkSize.x, std::max(vp->WorkSize.x * 0.38f, 380.0f * uiScale_));
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - width, vp->WorkPos.y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, vp->WorkSize.y), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(1.0f); // the strips behind must not show through the text
    if (!ImGui::Begin("Help", &showHelp_, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse
                                              | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }
    // Bullet + TextWrapped: BulletText never wraps in Dear ImGui.
    const auto item = [](const char* text) {
        ImGui::Bullet();
        ImGui::TextWrapped("%s", text);
    };
    ImGui::SeparatorText("Master (top row)");
    item("Output: where you listen. \"System default\" follows Windows; or pick your headphones or speakers.");
    item("Volume: the whole mix, 0-100%. The bar next to it shows how loud everything is together.");

    ImGui::SeparatorText("Buttons under it");
    item("+ Add channel: a new channel for apps.");
    item("+ Add mic channel: a channel for a microphone.");
    item("Presets: rename or delete your own EQ presets.");
    item("Theme: Dark, Midnight or Light.");
    item("Reset channels: back to Music, Game, Film, Chat, Podcast and Mic.");

    ImGui::SeparatorText("App channels");
    item("\"+ App\" puts apps in a channel. Pick as many as you like, e.g. Spotify and a browser in Music.");
    item("An app can be in one channel at a time. Picking it in another channel moves it there.");
    item("A closed app shows \"waiting for it to start\" and joins by itself when it starts.");
    item("The small x next to an app takes it out of the channel. Clear all empties the channel.");
    item("Apps you never put in a channel play normally, as if the mixer wasn't there.");
    item("\"Hear apps only through the mixer\" (in the + App window) moves each app's own sound to a spare output, "
         "so you don't hear it twice. With only one output device you will hear it twice. "
         "\"Reset all app outputs\" puts every app back to normal.");

    ImGui::SeparatorText("Mic channel");
    item("Choose your microphone in the list at the top of the channel.");
    item("It starts muted, so you don't hear yourself. The bar still moves when you talk, so you can see it works.");
    item("Unmute (M) to hear yourself, e.g. to check how you sound. Use headphones, or the speakers echo.");

    ImGui::SeparatorText("Every channel");
    item("Name: click it to rename. The x in the corner removes the channel.");
    item("Volume: 0-100%. The meter beside it is green when normal, yellow when loud, and red at the limit. "
         "The thin line shows the latest peak.");
    item("Balance: left or right; it snaps to the center.");
    item("M mutes the channel. S (solo) plays only the soloed channels.");

    ImGui::SeparatorText("Equalizer");
    item("10 sliders from deep bass (31 Hz, left) to treble (16k, right). Up is louder, down is quieter, the middle is unchanged.");
    item("The curve above the sliders shows the overall shape.");
    item("Pick a preset from the list. A * means you changed it. Save stores your own; pick Flat to undo every change.");
    ImGui::End();
}

void MixerUI::drawPresetRow(Strip& strip)
{
    const std::string label = strip.preset + (strip.presetModified ? " *" : "");
    ImGui::SetNextItemWidth(-60.0f);
    if (ImGui::BeginCombo("##preset", label.c_str())) {
        for (const Preset& p : presets_.presets()) {
            if (ImGui::Selectable(p.name.c_str(), p.name == strip.preset && !strip.presetModified)) {
                applyPreset(strip, p);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Save", ImVec2(-1.0f, 0))) {
        const Preset* current = presets_.find(strip.preset);
        copyToBuf(strip.presetNameBuf, current && !current->builtIn ? strip.preset : std::string());
        presetError_.clear();
        ImGui::OpenPopup("savepreset");
    }
    if (ImGui::BeginPopup("savepreset")) {
        ImGui::TextUnformatted("Save this EQ as a preset:");
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool enter = ImGui::InputText("##pname", strip.presetNameBuf.data(), strip.presetNameBuf.size(), ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::Button("Save") || enter) {
            const std::string name = strip.presetNameBuf.data();
            if (presets_.save(name, strip.channel->gains(), &presetError_)) {
                savePresets();
                strip.preset = name;
                strip.presetModified = false;
                ImGui::CloseCurrentPopup();
            }
        }
        if (!presetError_.empty()) {
            ImGui::TextColored(themeColors().errorText, "%s", presetError_.c_str());
        }
        ImGui::EndPopup();
    }
}

void MixerUI::drawEq(Strip& strip)
{
    Channel& ch = *strip.channel;
    EqGains gains = ch.gains();
    ImGui::BeginGroup();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3.0f, 4.0f));
    for (int b = 0; b < kEqBands; ++b) {
        if (b > 0) ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::PushID(b);
        if (ImGui::VSliderFloat("##band", ImVec2(kBandWidth, kSliderHeight), &gains[b], kEqMinGainDb, kEqMaxGainDb, "", ImGuiSliderFlags_NoInput)) {
            ch.setGain(b, gains[b]);
            strip.presetModified = true;
            strip.curveDirty = true;
        }
        if (ImGui::IsItemActive() || ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s Hz: %+.1f dB  (%s)", kEqBandLabels[b], gains[b],
                              gains[b] > 0.05f ? "louder" : gains[b] < -0.05f ? "quieter" : "unchanged");
        }
        const float textW = ImGui::CalcTextSize(kEqBandLabels[b]).x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (kBandWidth - textW) * 0.5f); // may hang over a little, like real EQ labels
        ImGui::TextUnformatted(kEqBandLabels[b]);
        ImGui::PopID();
        ImGui::EndGroup();
    }
    ImGui::PopStyleVar();
    ImGui::EndGroup();
}

void MixerUI::drawPresetManager()
{
    ImGui::SetNextWindowSize(ImVec2(420, 420), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Presets", &showPresetManager_, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }
    ImGui::TextDisabled("Save new presets with the Save button on any channel.");

    if (ImGui::BeginListBox("##list", ImVec2(-1.0f, 220.0f))) {
        for (const Preset& p : presets_.presets()) {
            const std::string label = p.name + (p.builtIn ? "  (built-in)" : "");
            if (ImGui::Selectable(label.c_str(), p.name == selectedPreset_)) {
                selectedPreset_ = p.name;
                copyToBuf(renameBuf_, p.name);
                presetError_.clear();
            }
        }
        ImGui::EndListBox();
    }

    const Preset* sel = presets_.find(selectedPreset_);
    if (sel) {
        std::string gains;
        for (int b = 0; b < kEqBands; ++b) {
            char buf[24];
            std::snprintf(buf, sizeof(buf), "%s:%+.0f ", kEqBandLabels[b], sel->gainsDb[b]);
            gains += buf;
        }
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", gains.c_str());
        ImGui::PopTextWrapPos();

        if (sel->builtIn) {
            ImGui::TextDisabled("Built-in presets cannot be renamed or deleted.");
        } else {
            ImGui::SetNextItemWidth(200.0f);
            ImGui::InputText("##rename", renameBuf_.data(), renameBuf_.size());
            ImGui::SameLine();
            if (ImGui::Button("Rename")) {
                const std::string from = selectedPreset_;
                const std::string to = renameBuf_.data();
                if (presets_.rename(from, to, &presetError_)) {
                    for (Strip& s : strips_) {
                        if (s.preset == from) s.preset = to;
                    }
                    selectedPreset_ = to;
                    savePresets();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete")) {
                const std::string name = selectedPreset_;
                if (presets_.remove(name, &presetError_)) {
                    for (Strip& s : strips_) {
                        if (s.preset == name) { // keep the sound, just drop the link
                            s.preset = "Custom";
                            s.presetModified = false;
                        }
                    }
                    selectedPreset_ = "Flat";
                    savePresets();
                }
            }
        }
    }
    if (!presetError_.empty()) {
        ImGui::TextColored(themeColors().errorText, "%s", presetError_.c_str());
    }
    ImGui::End();
}

} // namespace psm
