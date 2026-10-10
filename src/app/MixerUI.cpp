#include "MixerUI.h"

#include "Lang.h"
#include "OpenUrl.h"

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

const char* themeLabel(Theme theme)
{
    switch (theme) {
    case Theme::Midnight: return tr(S::ThemeMidnight);
    case Theme::Graphite: return tr(S::ThemeGraphite);
    case Theme::Violet: return tr(S::ThemeViolet);
    case Theme::Light: return tr(S::ThemeLight);
    case Theme::Dark: break;
    }
    return tr(S::ThemeDark);
}

// printf into a std::string, for the few labels built from a translated format.
template <typename... Args>
std::string format(S fmt, Args... args)
{
    char buf[256];
    std::snprintf(buf, sizeof(buf), tr(fmt), args...);
    return buf;
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
    setLanguage(languageFromCode(session.language));
    trayEnabled_ = session.trayEnabled;
    startWithWindows_ = session.startWithWindows;
    attachDeviceVolume();

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
        s->outputDevice = c.output;
        applyChannelOutput(*s); // before the apps, so none gets parked on this device
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
    session.masterVolume = percentToGain(masterVolumePct_); // the mixer gain is 1 while Windows sets the level
    session.outputDevice = engine_.requestedOutput();
    session.appAutoRoute = appAutoRoute_;
    session.appSilentOutput = appSilentOutput_;
    session.theme = themeName(theme_);
    session.language = languageCode(currentLanguage());
    session.trayEnabled = trayEnabled_;
    session.startWithWindows = startWithWindows_;
    for (const Strip& s : strips_) {
        const Channel& ch = *s.channel;
        ChannelConfig c;
        c.name = ch.name();
        c.kind = s.isMic ? "mic" : "apps";
        c.output = s.outputDevice;
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
        status_ = format(S::ChannelLimitFmt, Mixer::kMaxChannels);
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
    closeUnusedOutputs();
}

void MixerUI::applyChannelOutput(Strip& strip)
{
    std::string err;
    strip.channel->output = engine_.outputFor(strip.outputDevice, &err);
    strip.error = err; // on failure the choice is kept; it plays on Master meanwhile
}

void MixerUI::closeUnusedOutputs()
{
    std::vector<int> inUse;
    for (const Strip& s : strips_) {
        inUse.push_back(s.channel->output.load());
    }
    engine_.closeUnusedOutputs(inUse);
}

std::vector<std::string> MixerUI::outputsInUse() const
{
    std::vector<std::string> names{engine_.outputDeviceName()};
    for (const Strip& s : strips_) {
        if (!s.outputDevice.empty() && s.channel->output.load() > 0) names.push_back(s.outputDevice);
    }
    return names;
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
        strip.error = format(S::ChannelFullFmt, Channel::kMaxSources);
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
    fresh.language = current.language;
    fresh.trayEnabled = current.trayEnabled;
    fresh.startWithWindows = current.startWithWindows;
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
    const std::vector<std::string> inUse = outputsInUse(); // never park apps where you listen
    if (!appSilentOutput_.empty()) {
        for (const OutputDeviceInfo& d : outputDevices_) {
            if (d.name == appSilentOutput_ && !d.isDefault && std::find(inUse.begin(), inUse.end(), d.name) == inUse.end()) {
                return d.id;
            }
        }
    }
    return pickSilentOutputId(outputDevices_, inUse);
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
    attachDeviceVolume();
    // A channel may have picked the device that is now the Master: it then plays through Master.
    for (Strip& s : strips_) applyChannelOutput(s);
    closeUnusedOutputs();
    // The spare output for parked apps must never be the one the mixer now plays on.
    outputDevices_.clear();
    reopenAppChannels();
}

void MixerUI::attachDeviceVolume()
{
    if (!deviceVolume_) return;
    // "System default" follows Windows' default device; otherwise the device actually playing,
    // which is the default too when the picked one is unplugged.
    const bool attached = deviceVolume_->attach(engine_.requestedOutput().empty() ? std::string() : engine_.outputDeviceName());
    if (attached) {
        // One volume, not two stacked: the mix stays at full scale and Windows sets the level.
        engine_.mixer().masterVolume = 1.0f;
    } else {
        engine_.mixer().masterVolume = percentToGain(masterVolumePct_);
    }
}

std::string MixerUI::spareOutputName()
{
    const std::string id = silentOutputId();
    for (const OutputDeviceInfo& d : outputDevices_) {
        if (d.id == id) return d.name;
    }
    return {};
}

void MixerUI::drawSpareOutputCombo()
{
    // Captured apps' own sound is moved ("parked") here, so you hear them only through the mixer.
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr(S::SpareOutput));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr(S::SpareOutputTip));
    ImGui::SameLine();
    const std::string spare = spareOutputName();
    std::string label;
    if (!appAutoRoute_) label = tr(S::SpareOff);
    else if (spare.empty()) label = tr(S::SpareNone);
    else if (!appSilentOutput_.empty() && spare == appSilentOutput_) label = spare;
    else label = std::string(tr(S::Automatic)) + " (" + spare + ")";

    ImGui::SetNextItemWidth(280.0f);
    if (ImGui::BeginCombo("##spare", label.c_str())) {
        if (ImGui::IsWindowAppearing()) outputDevices_ = listOutputDevices();
        const std::vector<std::string> inUse = outputsInUse();
        if (ImGui::Selectable(tr(S::Automatic), appAutoRoute_ && appSilentOutput_.empty())) {
            appAutoRoute_ = true;
            appSilentOutput_.clear();
            reopenAppChannels();
        }
        int offered = 0;
        for (const OutputDeviceInfo& d : outputDevices_) {
            if (d.isDefault || std::find(inUse.begin(), inUse.end(), d.name) != inUse.end()) continue; // you listen there
            ++offered;
            if (ImGui::Selectable(d.name.c_str(), appAutoRoute_ && d.name == appSilentOutput_)) {
                appAutoRoute_ = true;
                appSilentOutput_ = d.name;
                reopenAppChannels();
            }
        }
        if (offered == 0) {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 320.0f);
            ImGui::TextColored(themeColors().warningText, "%s", tr(S::SpareNoDevice));
            ImGui::PopTextWrapPos();
        }
        ImGui::Separator();
        if (ImGui::Selectable(tr(S::SpareOff), !appAutoRoute_)) {
            appAutoRoute_ = false;
            reopenAppChannels();
        }
        if (ImGui::Selectable(tr(S::ResetAppOutputs))) {
            resetAllAppOutputs();
            reopenAppChannels();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tr(S::ResetAppOutputsTip));
        }
        ImGui::EndCombo();
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
        ImGui::TextDisabled("%s", tr(S::NoChannels));
    }
    ImGui::EndChild();

    if (pendingRemove_ >= 0 && pendingRemove_ < static_cast<int>(strips_.size())) {
        removeStrip(static_cast<size_t>(pendingRemove_));
    }
    pendingRemove_ = -1;

    ImGui::End();

    if (showHelp_) drawHelp();

    // Every edit ends with its widget letting go of the active state; one save per edit.
    const bool itemActive = ImGui::IsAnyItemActive();
    if (wasItemActive_ && !itemActive) sessionDirty_ = true;
    wasItemActive_ = itemActive;
}

void MixerUI::drawMasterBar(float dt)
{
    Mixer& mixer = engine_.mixer();
    const float rowEnd = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    // Keeps the next piece on this line only if it fits; a narrow window wraps the row instead of clipping it.
    const auto sameLineIfFits = [&](float nextWidth, float gap) {
        if (ImGui::GetItemRectMax().x + gap + nextWidth <= rowEnd) ImGui::SameLine(0.0f, gap);
    };

    ImGui::BeginGroup();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr(S::MasterOutput));
    ImGui::SameLine();

    // Where everything you hear comes out. "System default" follows Windows' default device.
    const std::string& requested = engine_.requestedOutput();
    const std::string label = requested.empty() ? std::string(tr(S::SystemDefault)) + " (" + engine_.outputDeviceName() + ")" : engine_.outputDeviceName();
    ImGui::SetNextItemWidth(300.0f);
    if (ImGui::BeginCombo("##output", label.c_str())) {
        if (ImGui::IsWindowAppearing()) playbackDevices_ = engine_.outputDeviceNames();
        if (ImGui::Selectable(tr(S::SystemDefault), requested.empty())) {
            selectOutput({});
        }
        for (const std::string& name : playbackDevices_) {
            if (ImGui::Selectable(name.c_str(), name == requested)) {
                selectOutput(name);
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr(S::MasterOutputTip));
    ImGui::EndGroup();

    sameLineIfFits(200.0f, spacing);
    ImGui::SetNextItemWidth(200.0f);
    // NoInput: a double-click must not turn the fader into a text box.
    if (deviceVolume_ && deviceVolume_->attached()) {
        // Mirrors the Windows volume, so headset buttons and the taskbar slider show up here at once.
        deviceVolume_->update();
        float pct = deviceVolume_->level() * 100.0f;
        const bool deviceMuted = deviceVolume_->muted();
        if (ImGui::SliderFloat("##master", &pct, 0.0f, 100.0f, tr(deviceMuted ? S::VolumeMutedFmt : S::VolumeFmt), ImGuiSliderFlags_NoInput)) {
            deviceVolume_->setLevel(pct * 0.01f);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr(deviceMuted ? S::MasterVolumeMutedTip : S::MasterVolumeSyncTip));
    } else if (ImGui::SliderFloat("##master", &masterVolumePct_, 0.0f, 100.0f, tr(S::VolumeFmt), ImGuiSliderFlags_NoInput)) {
        mixer.masterVolume = percentToGain(masterVolumePct_);
    }

    sameLineIfFits(240.0f, spacing);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetFrameHeight();
    for (int side = 0; side < 2; ++side) {
        Meter& m = masterMeter_[side];
        animating_ |= updateMeter(m.db, m.holdDb, m.holdSeconds, mixer.takeMasterPeak(side), dt);
        const float y0 = p.y + side * (h * 0.5f);
        drawMeterBar(ImGui::GetWindowDrawList(), ImVec2(p.x, y0 + 1), ImVec2(p.x + 240.0f, y0 + h * 0.5f - 1), m.db, m.holdDb, false, false);
    }
    ImGui::Dummy(ImVec2(240.0f, h));
    if (appCaptureSupported()) {
        sameLineIfFits(ImGui::CalcTextSize(tr(S::SpareOutput)).x + spacing + 280.0f, 24.0f);
        ImGui::BeginGroup();
        drawSpareOutputCombo();
        ImGui::EndGroup();
    }
}

void MixerUI::drawToolBar()
{
    const float rowEnd = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    if (ImGui::Button(tr(S::AddChannel))) {
        if (addStrip(format(S::NewChannelName, static_cast<int>(strips_.size() + 1)), false)) {
            scrollToNewStrip_ = true;
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr(S::AddChannelTip));
    ImGui::SameLine();
    if (ImGui::Button(tr(S::AddMicChannel))) {
        if (Strip* s = addStrip(tr(S::NewMicName), true)) {
            s->channel->mute = true; // never surprise anyone with their own voice on the speakers
            addInput(*s, "");
            scrollToNewStrip_ = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(tr(S::ResetChannels))) ImGui::OpenPopup("reset");
    const float leftEnd = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x; // right edge of Reset, in window space
    if (ImGui::BeginPopup("reset")) {
        ImGui::TextUnformatted(tr(S::ResetConfirm));
        ImGui::TextDisabled("%s", tr(S::ResetDetail));
        if (ImGui::Button(tr(S::Reset))) {
            resetChannels();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr(S::Cancel))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // Settings and Help sit at the right end of the row.
    const ImGuiStyle& style = ImGui::GetStyle();
    const float settingsW = ImGui::CalcTextSize(tr(S::Settings)).x + style.FramePadding.x * 2.0f;
    const float helpW = ImGui::CalcTextSize(tr(S::Help)).x + style.FramePadding.x * 2.0f;
    const float rightX = rowEnd - settingsW - helpW - style.ItemSpacing.x;
    if (!status_.empty()) {
        ImGui::SameLine();
        ImGui::PushClipRect(ImGui::GetCursorScreenPos(),
                            ImVec2(ImGui::GetWindowPos().x + rightX - style.ItemSpacing.x, ImGui::GetCursorScreenPos().y + ImGui::GetFrameHeight()),
                            true); // a long message never runs under the buttons
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(themeColors().errorText, "%s", status_.c_str());
        ImGui::PopClipRect();
    }
    // Only reachable below the minimum window size; the buttons then wrap instead of covering Reset.
    if (rightX >= leftEnd + style.ItemSpacing.x) ImGui::SameLine(rightX);
    if (ImGui::Button(tr(S::Settings))) ImGui::OpenPopup("settings");
    // Anchored under the button's right edge every frame, so it follows the button when the window
    // is resized and never opens past the window's right side.
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 anchor(std::min(ImGui::GetItemRectMax().x, vp->WorkPos.x + vp->WorkSize.x), ImGui::GetItemRectMax().y + style.ItemSpacing.y);
    ImGui::SetNextWindowPos(anchor, ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(vp->WorkSize.x, vp->WorkPos.y + vp->WorkSize.y - anchor.y));
    if (ImGui::BeginPopup("settings")) {
        drawSettings();
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(tr(S::Help))) showHelp_ = !showHelp_;
}

void MixerUI::drawSettings()
{
    // Labels in one column, wide enough for either language.
    const float labelW = std::max(ImGui::CalcTextSize(tr(S::ThemeLabel)).x, ImGui::CalcTextSize(tr(S::LanguageLabel)).x)
                       + ImGui::GetStyle().ItemSpacing.x * 2.0f;
    const float comboW = 180.0f * uiScale_;
    ImGui::SeparatorText(tr(S::Appearance));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr(S::ThemeLabel));
    ImGui::SameLine(labelW);
    ImGui::SetNextItemWidth(comboW);
    if (ImGui::BeginCombo("##theme", themeLabel(theme_))) {
        for (Theme t : {Theme::Dark, Theme::Midnight, Theme::Graphite, Theme::Violet, Theme::Light}) {
            if (ImGui::Selectable(themeLabel(t), t == theme_)) setTheme(t);
        }
        ImGui::EndCombo();
    }

    ImGui::SeparatorText(tr(S::LanguageLabel));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr(S::LanguageLabel));
    ImGui::SameLine(labelW);
    ImGui::SetNextItemWidth(comboW);
    // Each language is listed in its own name, so it can be found whatever is shown now.
    if (ImGui::BeginCombo("##language", languageName(currentLanguage()))) {
        for (int i = 0; i < static_cast<int>(Language::Count); ++i) {
            const auto lang = static_cast<Language>(i);
            if (ImGui::Selectable(languageName(lang), lang == currentLanguage())) setLanguage(lang);
        }
        ImGui::EndCombo();
    }

    if (trayChanged_ && autostartChanged_) {
        ImGui::SeparatorText(tr(S::SystemSection));
        if (ImGui::Checkbox(tr(S::TrayOption), &trayEnabled_)) trayChanged_(trayEnabled_);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr(S::TrayOptionTip));
        if (ImGui::Checkbox(tr(S::AutostartOption), &startWithWindows_)) autostartChanged_(startWithWindows_);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr(S::AutostartOptionTip));
    }
}

void MixerUI::releaseApps()
{
    // Every app capture puts its app's output back when destroyed; wait until that has happened.
    for (Strip& s : strips_) engine_.mixer().clearSources(s.channel);
    engine_.mixer().flushGarbage(500);
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
        ImGui::Text(tr(S::RemoveChannelFmt), ch.name().c_str());
        if (ImGui::Button(tr(S::Remove))) {
            pendingRemove_ = static_cast<int>(index);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr(S::Cancel))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (strip.isMic) drawMicSource(strip);
    else drawAppSources(strip);
    drawOutputRow(strip);
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
        ImGui::SetTooltip(tr(S::VolumeTipFmt), strip.volumePct);
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
    if (ImGui::SliderFloat("##pan", &pan, -1.0f, 1.0f, pan == 0.0f ? tr(S::PanCenter) : tr(pan < 0 ? S::PanLeftFmt : S::PanRightFmt),
                           ImGuiSliderFlags_NoInput)) {
        ch.pan = std::fabs(pan) < 0.05f ? 0.0f : pan; // snaps to center, so it is easy to hit
    }
    ImGui::SameLine();
    if (toggleButton("M", muted, themeColors().muteOn, ImVec2(32, 0))) ch.mute = !muted;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr(S::MuteTip));
    ImGui::SameLine();
    const bool soloed = ch.solo.load();
    if (toggleButton("S", soloed, themeColors().soloOn, ImVec2(32, 0))) ch.solo = !soloed;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr(S::SoloTip));

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
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr(S::RemoveFromChannelTip));
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        const auto* app = static_cast<AppSource*>(src);
        const std::string name = appDisplayName(app->exeName());
        switch (app->state()) {
        case AppSource::State::Capturing:
            ImGui::Text("%s", name.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%s", tr(app->isRerouted() ? S::AppOnlyViaMixer : S::AppPlaying));
            break;
        case AppSource::State::WaitingForApp:
            ImGui::Text("%s", name.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%s", tr(S::AppWaiting));
            break;
        case AppSource::State::Failed:
            ImGui::TextColored(themeColors().errorText, "%s: %s", name.c_str(), app->lastError().c_str());
            break;
        }
        ImGui::PopID();
    }
    if (shown == 0) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", tr(appCaptureSupported() ? S::NoAppsYet : S::AppsWindowsOnly));
        ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();

    ImGui::BeginDisabled(!appCaptureSupported());
    if (ImGui::Button(tr(S::AddApp))) {
        audioApps_ = listAudioApps();
        outputDevices_ = listOutputDevices();
        appExeBuf_[0] = '\0';
        ImGui::OpenPopup("app");
    }
    ImGui::EndDisabled();
    if (shown > 1) {
        ImGui::SameLine();
        if (ImGui::Button(tr(S::ClearAll))) {
            engine_.mixer().clearSources(&ch);
        }
    }

    if (ImGui::BeginPopup("app")) {
        ImGui::TextUnformatted(tr(S::AppsPlayingNow));
        for (const AudioAppInfo& app : audioApps_) {
            int slot = -1;
            const Strip* owner = findAppStrip(app.exeName, &slot);
            std::string label = app.displayName;
            if (owner == &strip) label += tr(S::InThisChannel);
            else if (owner) label += format(S::InOtherChannelFmt, owner->channel->name().c_str());
            // A checkbox shows at a glance that each row can be clicked, and what is already in.
            bool inHere = owner == &strip;
            ImGui::PushID(app.exeName.c_str());
            if (ImGui::Checkbox(label.c_str(), &inHere)) {
                if (inHere) addApp(strip, app.exeName);
                else removeSource(strip, slot);
            }
            ImGui::PopID();
        }
        if (audioApps_.empty()) {
            ImGui::TextDisabled("%s", tr(S::NoAppsNow));
        }
        ImGui::SetNextItemWidth(220.0f);
        const bool enter = ImGui::InputTextWithHint("##exe", tr(S::ExeHint), appExeBuf_.data(), appExeBuf_.size(),
                                                    ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if ((ImGui::Button(tr(S::Add)) || enter) && appExeBuf_[0] != '\0') {
            addApp(strip, appExeBuf_.data());
            appExeBuf_[0] = '\0';
        }
        ImGui::Separator();
        const std::string spare = appAutoRoute_ ? spareOutputName() : std::string();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 380.0f);
        if (spare.empty()) {
            ImGui::TextColored(themeColors().warningText, "%s", tr(S::HearTwiceWarning));
        } else {
            ImGui::TextDisabled(tr(S::MovedToFmt), spare.c_str());
        }
        ImGui::PopTextWrapPos();
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
                              : std::string();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr(S::Microphone));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    // kDefaultMicName is the engine's name for the default device; show it translated.
    const char* shown = current.empty() ? tr(S::None) : current == kDefaultMicName ? tr(S::DefaultMicrophone) : current.c_str();
    if (ImGui::BeginCombo("##mic", shown)) {
        if (ImGui::IsWindowAppearing()) captureDevices_ = engine_.captureDeviceNames();
        if (ImGui::Selectable(tr(S::DefaultMicrophone), current == kDefaultMicName)) addInput(strip, "");
        for (const std::string& name : captureDevices_) {
            if (ImGui::Selectable(name.c_str(), name == current)) addInput(strip, name);
        }
        if (captureDevices_.empty()) ImGui::TextDisabled("%s", tr(S::NoMicrophones));
        ImGui::Separator();
        if (ImGui::Selectable(tr(S::None), src == nullptr)) engine_.mixer().clearSources(&ch);
        ImGui::EndCombo();
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", tr(ch.mute.load() ? S::MicMutedHint : S::MicLiveHint));
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
    ImGui::Dummy(ImVec2(0.0f, ImGui::GetFrameHeight())); // lines up with the "+ App" row of app channels
}

void MixerUI::drawOutputRow(Strip& strip)
{
    // Where this channel plays. Automatic follows the Master output; a picked device stays picked.
    const bool following = strip.outputDevice.empty();
    std::string label = following ? tr(S::AutomaticMaster) : strip.outputDevice;
    if (!following && strip.channel->output.load() == 0 && strip.outputDevice != engine_.outputDeviceName()) {
        label += tr(S::NotConnected);
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr(S::Output));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##chout", label.c_str())) {
        if (ImGui::IsWindowAppearing()) playbackDevices_ = engine_.outputDeviceNames();
        std::string pick;
        bool picked = false;
        if (ImGui::Selectable(tr(S::AutomaticMaster), following)) {
            picked = true;
        }
        for (const std::string& name : playbackDevices_) {
            if (ImGui::Selectable(name.c_str(), name == strip.outputDevice)) {
                pick = name;
                picked = true;
            }
        }
        ImGui::EndCombo();
        if (picked && pick != strip.outputDevice) {
            strip.outputDevice = pick;
            applyChannelOutput(strip);
            closeUnusedOutputs();
            reopenAppChannels(); // a parked app may sit on the device this channel now uses
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr(S::ChannelOutputTip));
}

void MixerUI::drawHelp()
{
    // Docked to the right edge and sized from the main window every frame, so it follows resizes.
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float width = std::min(vp->WorkSize.x, std::max(vp->WorkSize.x * 0.38f, 380.0f * uiScale_));
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - width, vp->WorkPos.y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, vp->WorkSize.y), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(1.0f); // the strips behind must not show through the text
    if (!ImGui::Begin(tr(S::HelpTitle), &showHelp_, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse
                                              | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }
    // Bullet + TextWrapped: BulletText never wraps in Dear ImGui.
    const auto item = [](S text) {
        ImGui::Bullet();
        ImGui::TextWrapped("%s", tr(text));
    };
    if (ImGui::Button(tr(S::OpenGuide))) openUrl(tr(S::GuideUrl));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr(S::OpenGuideTip));

    ImGui::SeparatorText(tr(S::HelpMasterHead));
    item(S::HelpMasterOutput);
    item(S::HelpMasterVolume);

    ImGui::SeparatorText(tr(S::HelpButtonsHead));
    item(S::HelpAddChannel);
    item(S::HelpAddMic);
    item(S::HelpReset);
    item(S::HelpSettings);
    item(S::HelpHelp);

    ImGui::SeparatorText(tr(S::HelpAppsHead));
    item(S::HelpApps1);
    item(S::HelpApps2);
    item(S::HelpApps3);
    item(S::HelpApps4);
    item(S::HelpApps5);
    item(S::HelpApps6);

    ImGui::SeparatorText(tr(S::HelpMicHead));
    item(S::HelpMic1);
    item(S::HelpMic2);
    item(S::HelpMic3);

    ImGui::SeparatorText(tr(S::HelpChannelHead));
    item(S::HelpChannel1);
    item(S::HelpChannel2);
    item(S::HelpChannel3);
    item(S::HelpChannel4);
    item(S::HelpChannel5);

    ImGui::SeparatorText(tr(S::HelpEqHead));
    item(S::HelpEq1);
    item(S::HelpEq2);
    item(S::HelpEq3);
    item(S::HelpEq4);
    ImGui::End();
}

void MixerUI::drawPresetRow(Strip& strip)
{
    const std::string label = strip.preset + (strip.presetModified ? " *" : "");
    const ImGuiStyle& style = ImGui::GetStyle();
    const float saveW = ImGui::CalcTextSize(tr(S::Save)).x + style.FramePadding.x * 2.0f;
    const float delW = ImGui::CalcTextSize(tr(S::Delete)).x + style.FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(-(saveW + delW + style.ItemSpacing.x * 2.0f));
    if (ImGui::BeginCombo("##preset", label.c_str())) {
        for (const Preset& p : presets_.presets()) {
            if (ImGui::Selectable(p.name.c_str(), p.name == strip.preset && !strip.presetModified)) {
                applyPreset(strip, p);
            }
        }
        if (presets_.hasHiddenBuiltIns()) {
            ImGui::Separator();
            if (ImGui::Selectable(tr(S::RestoreBuiltIns))) {
                presets_.restoreBuiltIns();
                savePresets();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr(S::RestoreBuiltInsTip));
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button(tr(S::Save), ImVec2(saveW, 0))) {
        const Preset* current = presets_.find(strip.preset);
        copyToBuf(strip.presetNameBuf, current && !current->builtIn ? strip.preset : std::string());
        presetError_.clear();
        ImGui::OpenPopup("savepreset");
    }
    ImGui::SameLine();
    const bool deletable = presets_.canRemove(strip.preset);
    ImGui::BeginDisabled(!deletable);
    if (ImGui::Button(tr(S::Delete), ImVec2(delW, 0))) ImGui::OpenPopup("deletepreset");
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", tr(deletable ? S::DeletePresetTip : strip.preset == "Flat" ? S::FlatNotDeletable : S::PickPresetToDelete));
    }
    if (ImGui::BeginPopup("deletepreset")) {
        ImGui::Text(tr(S::DeletePresetFmt), strip.preset.c_str());
        ImGui::TextDisabled("%s", tr(S::DeletePresetDetail));
        if (ImGui::Button(tr(S::Delete))) {
            deletePreset(strip.preset);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr(S::Cancel))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("savepreset")) {
        ImGui::TextUnformatted(tr(S::SavePresetPrompt));
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool enter = ImGui::InputText("##pname", strip.presetNameBuf.data(), strip.presetNameBuf.size(), ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::Button(tr(S::Save)) || enter) {
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
            ImGui::SetTooltip(tr(S::BandTipFmt), kEqBandLabels[b], gains[b],
                              tr(gains[b] > 0.05f ? S::Louder : gains[b] < -0.05f ? S::Quieter : S::Unchanged));
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

void MixerUI::deletePreset(const std::string& name)
{
    if (!presets_.remove(name, &presetError_)) return;
    for (Strip& s : strips_) {
        if (s.preset == name) { // keep the sound, just drop the link
            s.preset = "Custom";
            s.presetModified = false;
        }
    }
    presetError_.clear();
    savePresets();
}

} // namespace psm
