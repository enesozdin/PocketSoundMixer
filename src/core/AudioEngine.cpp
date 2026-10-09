#include "AudioEngine.h"

#include "AppCapture.h"

#include "miniaudio.h"

#include <algorithm>
#include <atomic>
#include <cstring>

#if defined(__SSE__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 1)
    #include <xmmintrin.h>
    #define PSM_HAS_SSE 1
#endif

namespace psm {

void enableFlushDenormalsToZero()
{
#if defined(PSM_HAS_SSE)
    _mm_setcsr(_mm_getcsr() | 0x8040); // FTZ | DAZ
#elif defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
    uint64_t fpcr;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
    __asm__ __volatile__("msr fpcr, %0" : : "r"(fpcr | (1ull << 24)));
#endif
}

namespace {

std::string resultText(ma_result r)
{
    return ma_result_description(r);
}

// ---------------------------------------------------------------------------------------------
// Live capture. The capture device callback writes into a lock-free ring buffer that the
// playback callback drains. Two devices drift apart over time, so latency is clamped.
class InputSourceImpl final : public InputSource {
public:
    ~InputSourceImpl() override
    {
        if (deviceInitialized_) {
            ma_device_uninit(&device_); // stops and joins the capture thread
        }
        if (rbInitialized_) {
            ma_pcm_rb_uninit(&rb_);
        }
    }

    bool init(ma_context* ctx, const ma_device_id* id, std::string name, uint32_t sampleRate, std::string* error)
    {
        name_ = std::move(name);
        targetLatency_ = sampleRate / 50;  // 20 ms
        maxLatency_ = sampleRate / 20;     // 50 ms, beyond this we drop the backlog
        ma_result r = ma_pcm_rb_init(ma_format_f32, 2, sampleRate / 5, nullptr, nullptr, &rb_);
        if (r != MA_SUCCESS) {
            if (error) *error = "Ring buffer: " + resultText(r);
            return false;
        }
        rbInitialized_ = true;

        ma_device_config cfg = ma_device_config_init(ma_device_type_capture);
        cfg.capture.pDeviceID = id;
        cfg.capture.format = ma_format_f32;
        cfg.capture.channels = 2;
        cfg.sampleRate = sampleRate;
        cfg.performanceProfile = ma_performance_profile_low_latency;
        cfg.dataCallback = &InputSourceImpl::onCapture;
        cfg.pUserData = this;
        r = ma_device_init(ctx, &cfg, &device_);
        if (r != MA_SUCCESS) {
            if (error) *error = "Cannot open input \"" + name_ + "\": " + resultText(r);
            return false;
        }
        deviceInitialized_ = true;
        r = ma_device_start(&device_);
        if (r != MA_SUCCESS) {
            if (error) *error = "Cannot start input \"" + name_ + "\": " + resultText(r);
            return false;
        }
        return true;
    }

    void read(float* out, uint32_t frames) override
    {
        const ma_uint32 available = ma_pcm_rb_available_read(&rb_);
        if (available > maxLatency_ + frames) {
            ma_pcm_rb_seek_read(&rb_, available - targetLatency_ - frames);
        }
        uint32_t done = 0;
        while (done < frames) {
            ma_uint32 chunk = frames - done;
            void* ptr = nullptr;
            if (ma_pcm_rb_acquire_read(&rb_, &chunk, &ptr) != MA_SUCCESS || chunk == 0) {
                break;
            }
            std::memcpy(out + done * 2, ptr, chunk * 2 * sizeof(float));
            ma_pcm_rb_commit_read(&rb_, chunk);
            done += chunk;
        }
        if (done < frames) {
            std::memset(out + done * 2, 0, (frames - done) * 2 * sizeof(float));
        }
    }

    const std::string& deviceName() const override { return name_; }

private:
    static void onCapture(ma_device* device, void*, const void* input, ma_uint32 frames)
    {
        auto* self = static_cast<InputSourceImpl*>(device->pUserData);
        const float* in = static_cast<const float*>(input);
        ma_uint32 done = 0;
        while (done < frames) {
            ma_uint32 chunk = frames - done;
            void* ptr = nullptr;
            if (ma_pcm_rb_acquire_write(&self->rb_, &chunk, &ptr) != MA_SUCCESS || chunk == 0) {
                break; // reader stalled; drop instead of blocking the capture thread
            }
            std::memcpy(ptr, in + done * 2, chunk * 2 * sizeof(float));
            ma_pcm_rb_commit_write(&self->rb_, chunk);
            done += chunk;
        }
    }

    std::string name_;
    ma_pcm_rb rb_{};
    ma_device device_{};
    bool rbInitialized_ = false;
    bool deviceInitialized_ = false;
    ma_uint32 targetLatency_ = 960;
    ma_uint32 maxLatency_ = 2400;
};

} // namespace

// -------------------------------------------------------------------------------------------------
struct AudioEngine::Impl {
    ma_context context{};
    ma_device device{};
    bool contextReady = false;
    bool deviceReady = false;
    bool running = false;
    bool denormalsSet = false;
    std::string outputName = "No output device";
    std::string requestedOutput; // empty = system default
    std::unique_ptr<Mixer> mixer;
    std::vector<ma_device_info> playbackDevices;
    std::vector<ma_device_info> captureDevices;

    void refreshDevices()
    {
        playbackDevices.clear();
        captureDevices.clear();
        if (!contextReady) return;
        ma_device_info* playback = nullptr;
        ma_device_info* capture = nullptr;
        ma_uint32 playbackCount = 0;
        ma_uint32 captureCount = 0;
        if (ma_context_get_devices(&context, &playback, &playbackCount, &capture, &captureCount) == MA_SUCCESS) {
            playbackDevices.assign(playback, playback + playbackCount);
            captureDevices.assign(capture, capture + captureCount);
        }
    }

    // `sampleRate` 0 = the device's native rate (first start). Later opens keep the Mixer's
    // rate; miniaudio resamples only if the new device runs at a different one.
    ma_result openDevice(const std::string& name, uint32_t sampleRate)
    {
        const ma_device_id* id = nullptr;
        if (!name.empty()) {
            if (playbackDevices.empty()) refreshDevices();
            for (const ma_device_info& d : playbackDevices) {
                if (name == d.name) {
                    id = &d.id;
                    break;
                }
            }
        }
        ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
        cfg.playback.pDeviceID = id; // null: default device, and miniaudio follows default changes
        cfg.playback.format = ma_format_f32;
        cfg.playback.channels = 2;
        cfg.sampleRate = sampleRate;
        cfg.periodSizeInMilliseconds = 10;
        cfg.performanceProfile = ma_performance_profile_low_latency;
        cfg.noPreSilencedOutputBuffer = MA_TRUE; // Mixer::process overwrites the whole buffer
        cfg.noClip = MA_TRUE;                    // Mixer already clamps
        cfg.dataCallback = &Impl::onPlayback;
        cfg.pUserData = this;
        denormalsSet = false; // new device, new audio thread
        const ma_result r = ma_device_init(&context, &cfg, &device);
        if (r == MA_SUCCESS) {
            deviceReady = true;
            outputName = device.playback.name;
        }
        return r;
    }

    static void onPlayback(ma_device* device, void* output, const void*, ma_uint32 frames)
    {
        auto* self = static_cast<Impl*>(device->pUserData);
        if (!self->denormalsSet) {
            enableFlushDenormalsToZero(); // the callback always runs on the same thread
            self->denormalsSet = true;
        }
        self->mixer->process(static_cast<float*>(output), frames);
    }
};

AudioEngine::AudioEngine()
    : impl_(std::make_unique<Impl>())
{
}

AudioEngine::~AudioEngine()
{
    stop();
    if (impl_->deviceReady) {
        ma_device_uninit(&impl_->device);
    }
    impl_->mixer.reset();
    if (impl_->contextReady) {
        ma_context_uninit(&impl_->context);
    }
}

bool AudioEngine::start(const std::string& outputDevice, std::string* error)
{
    Impl& m = *impl_;
    if (m.running) {
        return true;
    }
    uint32_t sampleRate = 48000;
    ma_result r = MA_SUCCESS;
    if (!m.contextReady) {
        r = ma_context_init(nullptr, 0, nullptr, &m.context);
        m.contextReady = (r == MA_SUCCESS);
    }
    if (m.contextReady && !m.deviceReady) {
        m.requestedOutput = outputDevice;
        // Native rate on first start: no resampling on the output path.
        r = m.openDevice(outputDevice, m.mixer ? static_cast<uint32_t>(m.mixer->sampleRate()) : 0);
        if (r != MA_SUCCESS && !outputDevice.empty()) {
            r = m.openDevice({}, m.mixer ? static_cast<uint32_t>(m.mixer->sampleRate()) : 0); // device unplugged: fall back
        }
    }
    if (m.deviceReady) {
        sampleRate = m.device.sampleRate;
    }

    if (!m.mixer) {
        m.mixer = std::make_unique<Mixer>(static_cast<float>(sampleRate));
    }

    if (!m.deviceReady) {
        if (error) *error = "No audio output: " + resultText(r);
        return false;
    }
    r = ma_device_start(&m.device);
    if (r != MA_SUCCESS) {
        if (error) *error = "Cannot start audio output: " + resultText(r);
        return false;
    }
    m.running = true;
    return true;
}

void AudioEngine::stop()
{
    if (impl_->running) {
        ma_device_stop(&impl_->device);
        impl_->running = false;
    }
}

bool AudioEngine::isRunning() const { return impl_->running; }

Mixer& AudioEngine::mixer()
{
    if (!impl_->mixer) {
        impl_->mixer = std::make_unique<Mixer>(48000.0f);
    }
    return *impl_->mixer;
}

uint32_t AudioEngine::sampleRate() const
{
    return impl_->mixer ? static_cast<uint32_t>(impl_->mixer->sampleRate()) : 48000u;
}

const std::string& AudioEngine::outputDeviceName() const { return impl_->outputName; }

const std::string& AudioEngine::requestedOutput() const { return impl_->requestedOutput; }

std::vector<std::string> AudioEngine::outputDeviceNames()
{
    impl_->refreshDevices();
    std::vector<std::string> names;
    for (const auto& d : impl_->playbackDevices) {
        names.emplace_back(d.name);
    }
    return names;
}

bool AudioEngine::setOutputDevice(const std::string& name, std::string* error)
{
    Impl& m = *impl_;
    if (!m.contextReady) {
        if (error) *error = "Audio backend is not available";
        return false;
    }
    stop();
    if (m.deviceReady) {
        ma_device_uninit(&m.device); // joins the audio thread
        m.deviceReady = false;
    }
    m.requestedOutput = name;
    m.outputName = "No output device";
    const uint32_t rate = static_cast<uint32_t>(mixer().sampleRate());
    ma_result r = m.openDevice(name, rate);
    if (r != MA_SUCCESS && !name.empty()) {
        if (error) *error = "Cannot open \"" + name + "\", using the default output: " + resultText(r);
        m.requestedOutput.clear();
        r = m.openDevice({}, rate);
    }
    if (r != MA_SUCCESS) {
        if (error) *error = "No audio output: " + resultText(r);
        return false;
    }
    r = ma_device_start(&m.device);
    if (r != MA_SUCCESS) {
        if (error) *error = "Cannot start audio output: " + resultText(r);
        return false;
    }
    m.running = true;
    return true;
}

std::vector<std::string> AudioEngine::captureDeviceNames()
{
    impl_->refreshDevices();
    std::vector<std::string> names;
    for (const auto& d : impl_->captureDevices) {
        names.emplace_back(d.name);
    }
    return names;
}

std::unique_ptr<InputSource> AudioEngine::openInput(const std::string& deviceName, std::string* error)
{
    if (!impl_->contextReady) {
        if (error) *error = "Audio backend is not available";
        return nullptr;
    }
    if (impl_->captureDevices.empty()) {
        impl_->refreshDevices();
    }
    const ma_device_id* id = nullptr; // empty name = system default input
    if (!deviceName.empty()) {
        auto it = std::find_if(impl_->captureDevices.begin(), impl_->captureDevices.end(),
                               [&](const ma_device_info& d) { return deviceName == d.name; });
        if (it == impl_->captureDevices.end()) {
            if (error) *error = "Input device not found: " + deviceName;
            return nullptr;
        }
        id = &it->id;
    }
    auto src = std::make_unique<InputSourceImpl>();
    if (!src->init(&impl_->context, id, deviceName.empty() ? "Default microphone" : deviceName, sampleRate(), error)) {
        return nullptr;
    }
    return src;
}

std::unique_ptr<AppSource> AudioEngine::openApp(const std::string& exeName, const std::string& silentOutputId, std::string* error)
{
    return openAppCapture(exeName, sampleRate(), silentOutputId, error);
}

} // namespace psm
