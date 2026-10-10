// Core tests: no audio device needed, runs in CI on every platform.
#include "AudioEngine.h"
#include "GraphicEq.h"
#include "Mixer.h"
#include "Presets.h"
#include "Session.h"
#include "StereoRingBuffer.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <thread>
#include <chrono>
#include <algorithm>
#include <atomic>
#include <vector>

namespace {

int g_failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::printf("FAILED %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

#define CHECK_NEAR(a, b, tol) CHECK(std::fabs((a) - (b)) <= (tol))

constexpr float kRate = 48000.0f;
constexpr float kPi = 3.14159265358979f;

// Constant stereo value, records its own destruction.
class ConstSource final : public psm::AudioSource {
public:
    ConstSource(float value, bool* destroyed) : value_(value), destroyed_(destroyed) {}
    ~ConstSource() override { if (destroyed_) *destroyed_ = true; }
    void read(float* out, uint32_t frames) override
    {
        for (uint32_t i = 0; i < frames * 2; ++i) out[i] = value_;
    }
    psm::SourceKind kind() const override { return psm::SourceKind::Other; }

private:
    float value_;
    bool* destroyed_;
};

float rmsDb(const std::vector<float>& s, size_t from)
{
    double sum = 0.0;
    for (size_t i = from; i < s.size(); ++i) sum += double(s[i]) * s[i];
    return float(10.0 * std::log10(sum / double(s.size() - from)));
}

void testPeakingResponse()
{
    for (int b = 0; b < psm::kEqBands; ++b) {
        for (float g : {-12.0f, -6.0f, 3.0f, 12.0f}) {
            const auto c = psm::makePeakingCoeffs(kRate, psm::kEqBandFrequencies[b], psm::kEqBandQ, g);
            CHECK_NEAR(psm::biquadResponseDb(c, kRate, psm::kEqBandFrequencies[b]), g, 0.05f);
        }
    }
    // A 1 kHz band barely touches 31 Hz and 16 kHz.
    const auto c = psm::makePeakingCoeffs(kRate, 1000.0f, psm::kEqBandQ, 12.0f);
    CHECK(std::fabs(psm::biquadResponseDb(c, kRate, 31.5f)) < 0.5f);
    CHECK(std::fabs(psm::biquadResponseDb(c, kRate, 16000.0f)) < 0.5f);
}

void testFlatEqIsBitExact()
{
    psm::GraphicEq eq;
    eq.prepare(kRate);
    eq.setGains({});
    CHECK(eq.activeBandCount() == 0);

    std::mt19937 rng(1);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    std::vector<float> in(2048);
    for (auto& v : in) v = dist(rng);
    std::vector<float> out = in;
    eq.process(out.data(), 1024);
    CHECK(in == out);
}

// Runs a real sine through the float filter, so coefficient precision is covered too
// (the lowest band at 48 kHz is the hardest case for single precision).
void checkSineThroughBand(int band, float gainDb)
{
    psm::GraphicEq eq;
    eq.prepare(kRate);
    psm::EqGains gains{};
    gains[band] = gainDb;
    eq.setGains(gains);
    CHECK(eq.activeBandCount() == 1);

    const float freq = psm::kEqBandFrequencies[band];
    const uint32_t frames = 96000;
    std::vector<float> in(frames * 2);
    for (uint32_t n = 0; n < frames; ++n) {
        in[2 * n] = in[2 * n + 1] = 0.25f * std::sin(2.0f * kPi * freq * float(n) / kRate);
    }
    std::vector<float> out = in;
    eq.process(out.data(), frames);
    const size_t settle = 48000 * 2; // skip the start-up transient (long for the 31 Hz band)
    const float measured = rmsDb(out, settle) - rmsDb(in, settle);
    if (std::fabs(measured - gainDb) > 0.3f) {
        std::printf("  band %g Hz: expected %+.2f dB, measured %+.2f dB\n", freq, gainDb, measured);
    }
    CHECK_NEAR(measured, gainDb, 0.3f);
}

void testSineThroughBand()
{
    for (int b = 0; b < psm::kEqBands; ++b) {
        checkSineThroughBand(b, 6.0f);
        checkSineThroughBand(b, -9.0f);
    }
}

void testMixerSumMuteSoloPan()
{
    psm::Mixer mixer(kRate);
    psm::Channel* a = mixer.addChannel("A");
    psm::Channel* b = mixer.addChannel("B");
    mixer.replaceSource(a, 0, std::make_unique<ConstSource>(0.25f, nullptr));
    mixer.replaceSource(b, 0, std::make_unique<ConstSource>(0.125f, nullptr));

    std::vector<float> out(512 * 2);
    auto settle = [&] { mixer.process(out.data(), 512); mixer.process(out.data(), 512); };

    settle();
    CHECK_NEAR(out[1000], 0.375f, 1e-5f);

    a->mute = true;
    settle();
    CHECK_NEAR(out[1000], 0.125f, 1e-5f);

    a->mute = false;
    a->solo = true;
    settle();
    CHECK_NEAR(out[1000], 0.25f, 1e-5f);

    a->solo = false;
    a->pan = -1.0f; // hard left: right side of A goes silent
    settle();
    CHECK_NEAR(out[1000], 0.375f, 1e-5f);
    CHECK_NEAR(out[1001], 0.125f, 1e-5f);

    a->pan = 0.0f;
    a->volume = 0.5f;
    mixer.masterVolume = 0.5f;
    settle();
    CHECK_NEAR(out[1000], (0.125f + 0.125f) * 0.5f, 1e-5f);

    // Hard clamp at full scale.
    a->volume = 8.0f;
    mixer.masterVolume = 1.0f;
    settle();
    CHECK(out[1000] <= 1.0f);
    CHECK(mixer.takeMasterPeak(0) <= 1.0f);
}

void testRemoveIsDeferredUntilAudioThreadMovesOn()
{
    psm::Mixer mixer(kRate);
    bool destroyed = false;
    psm::Channel* a = mixer.addChannel("A");
    mixer.replaceSource(a, 0, std::make_unique<ConstSource>(0.5f, &destroyed));
    std::vector<float> out(256 * 2);
    mixer.process(out.data(), 256);

    mixer.removeChannel(a);
    CHECK(mixer.channels().empty());
    mixer.collectGarbage();
    CHECK(!destroyed); // the audio thread may still be inside a callback
    mixer.process(out.data(), 256);
    mixer.process(out.data(), 256);
    mixer.collectGarbage();
    CHECK(destroyed);
    CHECK_NEAR(out[100], 0.0f, 1e-9f);

    // flushGarbage waits for the audio thread instead of leaving the object for a later frame.
    bool flushed = false;
    psm::Channel* b = mixer.addChannel("B");
    mixer.replaceSource(b, 0, std::make_unique<ConstSource>(0.5f, &flushed));
    std::atomic<bool> running{true};
    std::thread audio([&] {
        std::vector<float> buf(64 * 2);
        while (running) {
            mixer.process(buf.data(), 64);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    mixer.replaceSource(b, 0, nullptr);
    mixer.flushGarbage(1000);
    CHECK(flushed);
    running = false;
    audio.join();
}

void testChannelLimit()
{
    psm::Mixer mixer(kRate);
    for (int i = 0; i < psm::Mixer::kMaxChannels; ++i) CHECK(mixer.addChannel("c") != nullptr);
    CHECK(mixer.addChannel("overflow") == nullptr);
}

void testPresetLibrary()
{
    psm::PresetLibrary lib;
    CHECK(lib.find("Flat") && lib.find("Flat")->builtIn);
    CHECK(lib.find("Chat") != nullptr);

    std::string err;
    psm::EqGains g{};
    g[0] = 3.0f;
    CHECK(!lib.save("Flat", g, &err));      // built-ins are read-only
    CHECK(lib.save("My Preset", g, &err));
    CHECK(lib.rename("My Preset", "Night", &err));
    CHECK(!lib.remove("Flat", &err));       // Flat always stays
    CHECK(!lib.canRemove("Flat") && lib.canRemove("Vocal"));
    CHECK(lib.remove("Vocal", &err));       // built-ins can be deleted...
    CHECK(lib.find("Vocal") == nullptr && lib.hasHiddenBuiltIns());
    CHECK(lib.save("Temp", g, &err));
    CHECK(lib.remove("Temp", &err));
    CHECK(lib.find("Temp") == nullptr);

    const auto file = std::filesystem::temp_directory_path() / "psm_test_presets.json";
    CHECK(lib.saveUserPresets(file, &err));
    psm::PresetLibrary loaded;
    CHECK(loaded.loadUserPresets(file, &err));
    CHECK(loaded.find("Night") && !loaded.find("Night")->builtIn);
    CHECK_NEAR(loaded.find("Night")->gainsDb[0], 3.0f, 1e-6f);
    CHECK(loaded.presets().size() == lib.presets().size());
    CHECK(loaded.find("Vocal") == nullptr);  // ...and stay deleted after a restart
    loaded.restoreBuiltIns();                // ...until restored, in their original place
    CHECK(loaded.find("Vocal") && loaded.find("Vocal")->builtIn && !loaded.hasHiddenBuiltIns());
    CHECK(loaded.presets()[3].name == "Vocal");
    CHECK(loaded.find("Night") != nullptr);
    std::filesystem::remove(file);
}

void testSeveralSourcesInOneChannel()
{
    psm::Mixer mixer(kRate);
    psm::Channel* a = mixer.addChannel("Music");
    bool firstGone = false;
    CHECK(mixer.addSource(a, std::make_unique<ConstSource>(0.25f, &firstGone)));
    CHECK(mixer.addSource(a, std::make_unique<ConstSource>(0.125f, nullptr)));
    CHECK(mixer.addSource(a, std::make_unique<ConstSource>(0.0625f, nullptr)));
    CHECK(a->sourceCount() == 3);

    std::vector<float> out(512 * 2);
    auto settle = [&] { mixer.process(out.data(), 512); mixer.process(out.data(), 512); };
    settle();
    CHECK_NEAR(out[1000], 0.4375f, 1e-5f); // all three summed

    mixer.replaceSource(a, 0, nullptr); // remove one app, the others keep playing
    settle();
    CHECK_NEAR(out[1000], 0.1875f, 1e-5f);
    mixer.collectGarbage();
    CHECK(firstGone);
    CHECK(a->freeSourceSlot() == 0); // the freed slot is reused first

    for (int i = a->sourceCount(); i < psm::Channel::kMaxSources; ++i) {
        CHECK(mixer.addSource(a, std::make_unique<ConstSource>(0.0f, nullptr)));
    }
    CHECK(!mixer.addSource(a, std::make_unique<ConstSource>(0.0f, nullptr)));

    mixer.clearSources(a);
    CHECK(a->sourceCount() == 0);
    settle();
    CHECK_NEAR(out[1000], 0.0f, 1e-9f);
}

void testChannelOnExtraOutput()
{
    psm::Mixer mixer(kRate);
    psm::Channel* music = mixer.addChannel("Music");
    psm::Channel* chat = mixer.addChannel("Chat");
    mixer.addSource(music, std::make_unique<ConstSource>(0.25f, nullptr));
    mixer.addSource(chat, std::make_unique<ConstSource>(0.5f, nullptr));
    chat->output = 1; // e.g. a headset

    std::vector<float> out(512 * 2);
    std::vector<float> extra(512 * 2);
    auto settle = [&] { mixer.process(out.data(), 512); mixer.process(out.data(), 512); };

    settle(); // no bus 1 yet: chat falls back to the Master device
    CHECK_NEAR(out[1000], 0.75f, 1e-5f);

    auto bus = std::make_unique<psm::OutputBus>(4096);
    psm::OutputBus* raw = bus.get();
    mixer.setOutputBus(1, std::move(bus));
    settle();
    CHECK_NEAR(out[1000], 0.25f, 1e-5f);        // Master hears only music
    CHECK(raw->ring.available() >= 1024);
    raw->ring.read(extra.data(), 512, 0, 100000);
    raw->ring.read(extra.data(), 512, 0, 100000);
    CHECK_NEAR(extra[1000], 0.5f, 1e-5f);       // the headset gets chat, master volume applied
    mixer.masterVolume = 0.5f;
    settle();
    raw->ring.read(extra.data(), 512, 0, 100000);
    raw->ring.read(extra.data(), 512, 0, 100000);
    CHECK_NEAR(extra[1000], 0.25f, 1e-5f);

    mixer.setOutputBus(1, nullptr); // device closed: back to Master, bus freed later
    mixer.masterVolume = 1.0f;
    settle();
    CHECK_NEAR(out[1000], 0.75f, 1e-5f);
    mixer.collectGarbage();
}

void testMutedChannelStillMeters()
{
    psm::Mixer mixer(kRate);
    psm::Channel* mic = mixer.addChannel("Mic");
    mixer.addSource(mic, std::make_unique<ConstSource>(0.5f, nullptr));
    mic->mute = true;
    mic->volume = 0.25f;
    std::vector<float> out(512 * 2);
    mixer.process(out.data(), 512);
    mixer.process(out.data(), 512);
    mic->takePeak(0);
    mixer.process(out.data(), 512);
    CHECK_NEAR(out[1000], 0.0f, 1e-9f);            // not heard
    CHECK_NEAR(mic->takePeak(0), 0.125f, 1e-5f);   // but the meter shows what you would hear
}

void testSessionRoundTripAndOldFormat()
{
    std::string err;
    const psm::SessionConfig def = psm::defaultSession();
    CHECK(def.channels.size() == 6);
    CHECK(def.channels.back().name == "Mic" && def.channels.back().mute && def.channels.back().kind == "mic");
    CHECK(def.channels.front().kind == "apps");
    CHECK(def.channels.back().sources.size() == 1 && def.channels.back().sources[0].type == "input");

    psm::SessionConfig s = def;
    s.outputDevice = "Headphones";
    s.theme = "Light";
    s.channels[0].sources = {{"app", "Spotify.exe", {}}, {"app", "chrome.exe", {}}};
    s.channels[3].output = "Headset";
    const auto file = std::filesystem::temp_directory_path() / "psm_test_session.json";
    CHECK(psm::saveSession(file, s, &err));
    psm::SessionConfig loaded;
    CHECK(psm::loadSession(file, loaded, &err));
    CHECK(loaded.outputDevice == "Headphones");
    CHECK(loaded.theme == "Light");
    CHECK(loaded.channels[3].output == "Headset" && loaded.channels[0].output.empty());
    CHECK(loaded.channels.back().kind == "mic" && loaded.channels.back().sources.size() == 1);
    CHECK(loaded.channels.size() == 6);
    CHECK(loaded.channels[0].sources.size() == 2 && loaded.channels[0].sources[1].appExe == "chrome.exe");

    // Layouts saved by earlier test builds (before version 3) are dropped: the app starts with the six defaults.
    {
        std::ofstream out(file, std::ios::trunc);
        out << R"({"version":2,"master":0.5,"channels":[{"name":"A"},{"name":"B"},{"name":"C"},{"name":"D"},
            {"name":"E"},{"name":"F"},{"name":"G"},{"name":"H"},{"name":"I"}]})";
    }
    psm::SessionConfig old;
    CHECK(!psm::loadSession(file, old, &err));
    std::filesystem::remove(file);
}

void testEngineStartsWithMissingOutputDevice()
{
    // Works with or without a sound card: an unplugged saved device falls back to the default.
    psm::AudioEngine engine;
    std::string err;
    engine.start("Device that does not exist", &err);
    CHECK(engine.sampleRate() > 0);
    engine.mixer(); // always available, even with no device
    engine.stop();
}

void testRingBuffer()
{
    psm::StereoRingBuffer rb(100);
    CHECK(rb.capacity() == 128);

    // Wraps around the end correctly.
    std::vector<float> in(2 * 100), out(2 * 100);
    float next = 0.0f;
    float expect = 0.0f;
    bool ordered = true;
    for (int round = 0; round < 10; ++round) {
        for (auto& v : in) v = next++;
        CHECK(rb.write(in.data(), 70) == 70);
        CHECK(rb.read(out.data(), 70, 0, 1000) == 70);
        for (int i = 0; i < 140; ++i) {
            ordered &= out[i] == expect++;
        }
        next = expect;
    }
    CHECK(ordered);

    // Full buffer drops instead of overwriting.
    CHECK(rb.write(in.data(), 100) == 100);
    CHECK(rb.write(in.data(), 100) == 28);
    CHECK(rb.available() == 128);

    // A large backlog is trimmed to the target latency.
    std::vector<float> small(2 * 16);
    CHECK(rb.read(small.data(), 16, 32, 64) == 16);
    CHECK(rb.available() == 32);

    // Underrun zero-fills.
    psm::StereoRingBuffer empty(64);
    small.assign(small.size(), 1.0f);
    CHECK(empty.read(small.data(), 16, 0, 64) == 0);
    CHECK(small[0] == 0.0f && small[31] == 0.0f);
    CHECK(empty.writeSilence(8) == 8);
    CHECK(empty.available() == 8);
}

} // namespace

int main()
{
    testPeakingResponse();
    testFlatEqIsBitExact();
    testSineThroughBand();
    testMixerSumMuteSoloPan();
    testRemoveIsDeferredUntilAudioThreadMovesOn();
    testChannelLimit();
    testPresetLibrary();
    testRingBuffer();
    testSeveralSourcesInOneChannel();
    testMutedChannelStillMeters();
    testChannelOnExtraOutput();
    testSessionRoundTripAndOldFormat();
    testEngineStartsWithMissingOutputDevice();
    if (g_failures == 0) {
        std::printf("All core tests passed\n");
        return 0;
    }
    std::printf("%d check(s) failed\n", g_failures);
    return 1;
}
