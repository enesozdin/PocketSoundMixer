#include "MixerUI.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace psm {

namespace {

constexpr float kMinFaderDb = -60.0f; // fader bottom = silence
constexpr float kMaxFaderDb = 6.0f;
constexpr float kMeterFloorDb = -60.0f;
constexpr float kMeterFallDbPerSec = 24.0f;
constexpr float kStripWidth = 326.0f;
constexpr float kSliderHeight = 160.0f;
constexpr float kBandWidth = 22.0f;

float faderDbToLinear(float db)
{
    return db <= kMinFaderDb ? 0.0f : std::pow(10.0f, db / 20.0f);
}

float linearToFaderDb(float lin)
{
    return lin <= 0.0f ? kMinFaderDb : std::clamp(20.0f * std::log10(lin), kMinFaderDb, kMaxFaderDb);
}

template <size_t N>
void copyToBuf(std::array<char, N>& buf, const std::string& s)
{
    const size_t n = std::min(s.size(), N - 1);
    std::memcpy(buf.data(), s.data(), n);
    buf[n] = '\0';
}

std::string fileStem(const std::string& utf8Path)
{
    const size_t slash = utf8Path.find_last_of("/\\");
    std::string name = slash == std::string::npos ? utf8Path : utf8Path.substr(slash + 1);
    const size_t dot = name.find_last_of('.');
    return dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
}

std::string formatTime(double seconds)
{
    const int s = static_cast<int>(seconds);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%02d", s / 60, s % 60);
    return buf;
}

// Peak meters: falls smoothly, jumps up instantly. Returns true while it is still moving.
bool updateMeter(float& displayDb, float peakLinear, float dt)
{
    const float peakDb = peakLinear > 0.0f ? 20.0f * std::log10(peakLinear) : -90.0f;
    const float before = displayDb;
    displayDb = std::max(peakDb, displayDb - kMeterFallDbPerSec * dt);
    displayDb = std::max(displayDb, -90.0f);
    return displayDb > kMeterFloorDb || before > kMeterFloorDb;
}

void drawMeterBar(ImDrawList* dl, ImVec2 min, ImVec2 max, float db, bool vertical)
{
    dl->AddRectFilled(min, max, IM_COL32(30, 30, 34, 255));
    const float t = std::clamp((db - kMeterFloorDb) / (kMaxFaderDb - kMeterFloorDb), 0.0f, 1.0f);
    if (t <= 0.0f) return;
    const ImU32 col = db > -0.1f ? IM_COL32(230, 70, 60, 255)    // clipping
                    : db > -6.0f ? IM_COL32(230, 200, 60, 255)   // hot
                                 : IM_COL32(80, 200, 120, 255);
    if (vertical) {
        dl->AddRectFilled(ImVec2(min.x, max.y - (max.y - min.y) * t), max, col);
    } else {
        dl->AddRectFilled(min, ImVec2(min.x + (max.x - min.x) * t, max.y), col);
    }
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
    masterVolumeDb_ = linearToFaderDb(session.masterVolume);

    for (const ChannelConfig& c : session.channels) {
        Strip* s = addStrip(c.name);
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
        s->volumeDb = linearToFaderDb(c.volume);
        ch.pan = c.pan;
        ch.mute = c.mute;
        ch.solo = c.solo;
        if (c.sourceType == "file" && !c.filePath.empty()) {
            loadFile(*s, c.filePath, c.loop);
        } else if (c.sourceType == "input") {
            loadInput(*s, c.inputDevice);
        }
    }
}

SessionConfig MixerUI::captureSession() const
{
    SessionConfig session;
    session.masterVolume = engine_.mixer().masterVolume.load();
    for (const Strip& s : strips_) {
        const Channel& ch = *s.channel;
        ChannelConfig c;
        c.name = ch.name();
        c.preset = s.preset;
        c.gainsDb = ch.gains();
        c.volume = ch.volume.load();
        c.pan = ch.pan.load();
        c.mute = ch.mute.load();
        c.solo = ch.solo.load();
        if (AudioSource* src = ch.source()) {
            if (src->kind() == SourceKind::File) {
                const auto* f = static_cast<const FileSource*>(src);
                c.sourceType = "file";
                c.filePath = f->path();
                c.loop = f->isLooping();
            } else if (src->kind() == SourceKind::Input) {
                const auto* in = static_cast<const InputSource*>(src);
                c.sourceType = "input";
                c.inputDevice = in->deviceName() == "Default input" ? "" : in->deviceName();
            }
        }
        session.channels.push_back(std::move(c));
    }
    return session;
}

// ---------------------------------------------------------------------------------------------
// Actions

MixerUI::Strip* MixerUI::addStrip(const std::string& name)
{
    Channel* ch = engine_.mixer().addChannel(name);
    if (!ch) {
        status_ = "Channel limit reached (" + std::to_string(Mixer::kMaxChannels) + ")";
        return nullptr;
    }
    Strip s;
    s.channel = ch;
    copyToBuf(s.nameBuf, name);
    strips_.push_back(s);
    return &strips_.back();
}

void MixerUI::removeStrip(size_t index)
{
    engine_.mixer().removeChannel(strips_[index].channel);
    strips_.erase(strips_.begin() + static_cast<std::ptrdiff_t>(index));
}

void MixerUI::loadFile(Strip& strip, const std::string& utf8Path, bool loop)
{
    std::string err;
    std::unique_ptr<FileSource> src = engine_.openFile(utf8Path, &err);
    if (!src) {
        strip.error = err;
        return;
    }
    src->setLooping(loop);
    engine_.mixer().replaceSource(strip.channel, std::move(src));
    copyToBuf(strip.pathBuf, utf8Path);
    strip.error.clear();
}

void MixerUI::loadInput(Strip& strip, const std::string& deviceName)
{
    std::string err;
    std::unique_ptr<InputSource> src = engine_.openInput(deviceName, &err);
    if (!src) {
        strip.error = err;
        return;
    }
    engine_.mixer().replaceSource(strip.channel, std::move(src));
    strip.error.clear();
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

void MixerUI::onFilesDropped(const std::vector<std::string>& utf8Paths, float x, float y)
{
    for (size_t i = 0; i < utf8Paths.size(); ++i) {
        Strip* target = nullptr;
        if (i == 0) { // first file goes to the strip under the cursor
            for (Strip& s : strips_) {
                if (x >= s.rectMin[0] && x <= s.rectMax[0] && y >= s.rectMin[1] && y <= s.rectMax[1]) {
                    target = &s;
                    break;
                }
            }
        }
        if (!target) {
            target = addStrip(fileStem(utf8Paths[i]));
            if (!target) return;
        }
        loadFile(*target, utf8Paths[i], true);
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

    drawTopBar(dt);
    ImGui::Separator();

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
        ImGui::TextDisabled("No channels. Click \"+ Add channel\" or drop audio files here.");
    }
    ImGui::EndChild();

    if (pendingRemove_ >= 0 && pendingRemove_ < static_cast<int>(strips_.size())) {
        removeStrip(static_cast<size_t>(pendingRemove_));
    }
    pendingRemove_ = -1;

    ImGui::End();

    if (showPresetManager_) {
        drawPresetManager();
    }
}

void MixerUI::drawTopBar(float dt)
{
    Mixer& mixer = engine_.mixer();

    if (ImGui::Button("+ Add channel")) {
        if (addStrip("Channel " + std::to_string(strips_.size() + 1))) {
            scrollToNewStrip_ = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Presets")) {
        showPresetManager_ = !showPresetManager_;
    }
    ImGui::SameLine();
    ImGui::TextUnformatted("Master");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::SliderFloat("##master", &masterVolumeDb_, kMinFaderDb, kMaxFaderDb,
                           masterVolumeDb_ <= kMinFaderDb ? "-inf dB" : "%.1f dB")) {
        mixer.masterVolume = faderDbToLinear(masterVolumeDb_);
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        masterVolumeDb_ = 0.0f;
        mixer.masterVolume = 1.0f;
    }
    ImGui::SameLine();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetFrameHeight();
    for (int side = 0; side < 2; ++side) {
        animating_ |= updateMeter(masterMeterDb_[side], mixer.takeMasterPeak(side), dt);
        const float y0 = p.y + side * (h * 0.5f);
        drawMeterBar(ImGui::GetWindowDrawList(), ImVec2(p.x, y0 + 1), ImVec2(p.x + 200.0f, y0 + h * 0.5f - 1), masterMeterDb_[side], false);
    }
    ImGui::Dummy(ImVec2(200.0f, h));
    ImGui::SameLine();
    ImGui::TextDisabled("%s  |  %u Hz", engine_.outputDeviceName().c_str(), engine_.sampleRate());
    if (!status_.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s", status_.c_str());
    }
}

void MixerUI::drawStrip(Strip& strip, size_t index, float dt)
{
    Channel& ch = *strip.channel;
    ImGui::PushID(strip.channel);
    ImGui::BeginChild("strip", ImVec2(kStripWidth, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    const ImVec2 wp = ImGui::GetWindowPos();
    const ImVec2 ws = ImGui::GetWindowSize();
    strip.rectMin[0] = wp.x;
    strip.rectMin[1] = wp.y;
    strip.rectMax[0] = wp.x + ws.x;
    strip.rectMax[1] = wp.y + ws.y;

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

    drawSourceRow(strip);
    drawPresetRow(strip);
    if (strip.curveDirty) {
        updateCurve(strip);
    }
    ImGui::PlotLines("##curve", strip.curve.data(), kCurvePoints, 0, nullptr, -18.0f, 18.0f, ImVec2(-1.0f, 44.0f));

    drawEq(strip);

    // Fader + meters on the right of the EQ.
    ImGui::SameLine();
    ImGui::BeginGroup();
    if (ImGui::VSliderFloat("##vol", ImVec2(26.0f, kSliderHeight), &strip.volumeDb, kMinFaderDb, kMaxFaderDb, "")) {
        ch.volume = faderDbToLinear(strip.volumeDb);
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        strip.volumeDb = 0.0f;
        ch.volume = 1.0f;
    }
    if (ImGui::IsItemActive() || ImGui::IsItemHovered()) {
        ImGui::SetTooltip(strip.volumeDb <= kMinFaderDb ? "-inf dB" : "%.1f dB", strip.volumeDb);
    }
    ImGui::TextUnformatted("Vol");
    ImGui::EndGroup();

    ImGui::SameLine();
    const ImVec2 mp = ImGui::GetCursorScreenPos();
    for (int side = 0; side < 2; ++side) {
        animating_ |= updateMeter(strip.meterDb[side], ch.takePeak(side), dt);
        const float x0 = mp.x + side * 7.0f;
        drawMeterBar(ImGui::GetWindowDrawList(), ImVec2(x0, mp.y), ImVec2(x0 + 5.0f, mp.y + kSliderHeight), strip.meterDb[side], true);
    }
    ImGui::Dummy(ImVec2(14.0f, kSliderHeight));

    // Pan, mute, solo.
    float pan = ch.pan.load();
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::SliderFloat("##pan", &pan, -1.0f, 1.0f, pan == 0.0f ? "Center" : (pan < 0 ? "L %.2f" : "R %.2f"))) {
        ch.pan = pan;
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        ch.pan = 0.0f;
    }
    ImGui::SameLine();
    const bool muted = ch.mute.load();
    if (toggleButton("M", muted, ImVec4(0.75f, 0.25f, 0.2f, 1.0f), ImVec2(32, 0))) ch.mute = !muted;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Mute");
    ImGui::SameLine();
    const bool soloed = ch.solo.load();
    if (toggleButton("S", soloed, ImVec4(0.8f, 0.65f, 0.1f, 1.0f), ImVec2(32, 0))) ch.solo = !soloed;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Solo: only soloed channels play");

    if (!strip.error.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", strip.error.c_str());
        ImGui::PopTextWrapPos();
    }

    ImGui::EndChild();
    ImGui::PopID();
}

void MixerUI::drawSourceRow(Strip& strip)
{
    Channel& ch = *strip.channel;
    AudioSource* src = ch.source();

    // Fixed three-row layout so every strip's EQ lines up, whatever its source.
    if (src && src->kind() == SourceKind::File) {
        auto* f = static_cast<FileSource*>(src);
        ImGui::TextDisabled("File: %s", fileStem(f->path()).c_str());
        const bool playing = f->isPlaying();
        if (ImGui::Button(playing ? "Pause" : "Play", ImVec2(52, 0))) {
            f->setPlaying(!playing);
        }
        ImGui::SameLine();
        bool loop = f->isLooping();
        if (ImGui::Checkbox("Loop", &loop)) {
            f->setLooping(loop);
        }
        ImGui::SameLine();
        const double rate = engine_.sampleRate();
        const uint64_t len = f->lengthFrames();
        if (len > 0) {
            float pos = static_cast<float>(static_cast<double>(f->cursorFrames()) / static_cast<double>(len));
            const std::string label = formatTime(static_cast<double>(f->cursorFrames()) / rate) + " / " + formatTime(static_cast<double>(len) / rate);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::SliderFloat("##seek", &pos, 0.0f, 1.0f, label.c_str())) {
                f->requestSeek(static_cast<uint64_t>(pos * static_cast<double>(len)));
            }
        } else {
            ImGui::TextDisabled("%s", formatTime(static_cast<double>(f->cursorFrames()) / rate).c_str());
        }
        animating_ |= playing;
    } else {
        if (src && src->kind() == SourceKind::Input) {
            ImGui::TextDisabled("Live input: %s", static_cast<InputSource*>(src)->deviceName().c_str());
        } else {
            ImGui::TextDisabled("No source. Drop an audio file here.");
        }
        ImGui::Dummy(ImVec2(0.0f, ImGui::GetFrameHeight()));
    }

    if (ImGui::Button("File...")) {
        ImGui::OpenPopup("file");
    }
    ImGui::SameLine();
    if (ImGui::Button("Input...")) {
        captureDevices_ = engine_.captureDeviceNames();
        ImGui::OpenPopup("input");
    }
    if (src) {
        ImGui::SameLine();
        if (ImGui::Button("Clear")) {
            engine_.mixer().replaceSource(&ch, nullptr);
        }
    }

    if (ImGui::BeginPopup("file")) {
        ImGui::TextUnformatted("WAV, MP3 or FLAC path (or drag a file onto the strip):");
        ImGui::SetNextItemWidth(420.0f);
        const bool enter = ImGui::InputText("##path", strip.pathBuf.data(), strip.pathBuf.size(), ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::Button("Load") || enter) {
            loadFile(strip, strip.pathBuf.data(), true);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("input")) {
        if (ImGui::Selectable("Default input")) {
            loadInput(strip, "");
        }
        for (const std::string& name : captureDevices_) {
            if (ImGui::Selectable(name.c_str())) {
                loadInput(strip, name);
            }
        }
        if (captureDevices_.empty()) {
            ImGui::TextDisabled("No input devices found");
        }
        ImGui::EndPopup();
    }
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
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", presetError_.c_str());
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
        if (ImGui::VSliderFloat("##band", ImVec2(kBandWidth, kSliderHeight), &gains[b], kEqMinGainDb, kEqMaxGainDb, "")) {
            ch.setGain(b, gains[b]);
            strip.presetModified = true;
            strip.curveDirty = true;
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            ch.setGain(b, 0.0f);
            strip.presetModified = true;
            strip.curveDirty = true;
        }
        if (ImGui::IsItemActive() || ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s Hz: %+.1f dB", kEqBandLabels[b], gains[b]);
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
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", presetError_.c_str());
    }
    ImGui::End();
}

} // namespace psm
