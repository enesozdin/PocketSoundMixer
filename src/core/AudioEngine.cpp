#include "AudioEngine.h"

#include "AppCapture.h"

#include "miniaudio.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>

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
// File playback. The resource manager decodes into pages on its own job thread, already
// converted to f32 stereo at the device rate, so read() only copies.
class FileSourceImpl final : public FileSource {
public:
    ~FileSourceImpl() override
    {
        if (initialized_) {
            ma_resource_manager_data_source_uninit(&ds_);
        }
    }

    bool init(ma_resource_manager* rm, const std::string& utf8Path, std::string* error)
    {
        path_ = utf8Path;
        const ma_uint32 flags = MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_STREAM;
#if defined(_WIN32)
        // The char* API is not UTF-8 safe on Windows; go through wide strings.
        const std::filesystem::path p(std::u8string(reinterpret_cast<const char8_t*>(utf8Path.data()), utf8Path.size()));
        const ma_result r = ma_resource_manager_data_source_init_w(rm, p.wstring().c_str(), flags, nullptr, &ds_);
#else
        const ma_result r = ma_resource_manager_data_source_init(rm, utf8Path.c_str(), flags, nullptr, &ds_);
#endif
        if (r != MA_SUCCESS) {
            if (error) {
                *error = "Cannot open \"" + utf8Path + "\": " + resultText(r);
            }
            return false;
        }
        initialized_ = true;
        ma_uint64 length = 0;
        if (ma_data_source_get_length_in_pcm_frames(&ds_, &length) == MA_SUCCESS) {
            length_ = length;
        }
        return true;
    }

    void read(float* out, uint32_t frames) override
    {
        const int64_t seek = seekRequest_.exchange(-1, std::memory_order_acq_rel);
        if (seek >= 0) {
            ma_data_source_seek_to_pcm_frame(&ds_, static_cast<ma_uint64>(seek));
        }
        const bool loop = looping_.load(std::memory_order_relaxed);
        if (loop != appliedLooping_) {
            ma_data_source_set_looping(&ds_, loop ? MA_TRUE : MA_FALSE);
            appliedLooping_ = loop;
        }

        ma_uint64 framesRead = 0;
        ma_result r = MA_SUCCESS;
        if (playing_.load(std::memory_order_relaxed)) {
            // MA_BUSY means the next page is not decoded yet; we simply output silence for it.
            r = ma_data_source_read_pcm_frames(&ds_, out, frames, &framesRead);
        }
        if (framesRead < frames) {
            std::memset(out + framesRead * 2, 0, (frames - framesRead) * 2 * sizeof(float));
        }
        if (r == MA_AT_END && !loop) {
            playing_.store(false, std::memory_order_relaxed);
            ma_data_source_seek_to_pcm_frame(&ds_, 0);
        }
        ma_uint64 cursor = 0;
        if (ma_data_source_get_cursor_in_pcm_frames(&ds_, &cursor) == MA_SUCCESS) {
            cursor_.store(cursor, std::memory_order_relaxed);
        }
    }

    const std::string& path() const override { return path_; }
    bool isPlaying() const override { return playing_.load(std::memory_order_relaxed); }
    void setPlaying(bool playing) override { playing_.store(playing, std::memory_order_relaxed); }
    bool isLooping() const override { return looping_.load(std::memory_order_relaxed); }
    void setLooping(bool looping) override { looping_.store(looping, std::memory_order_relaxed); }
    uint64_t lengthFrames() const override { return length_; }
    uint64_t cursorFrames() const override { return cursor_.load(std::memory_order_relaxed); }
    void requestSeek(uint64_t frame) override { seekRequest_.store(static_cast<int64_t>(frame), std::memory_order_release); }

private:
    ma_resource_manager_data_source ds_{};
    bool initialized_ = false;
    std::string path_;
    uint64_t length_ = 0;
    std::atomic<bool> playing_{true};
    std::atomic<bool> looping_{true};
    bool appliedLooping_ = false;
    std::atomic<uint64_t> cursor_{0};
    std::atomic<int64_t> seekRequest_{-1};
};

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
    ma_resource_manager resourceManager{};
    bool contextReady = false;
    bool deviceReady = false;
    bool resourceManagerReady = false;
    bool running = false;
    bool denormalsSet = false;
    std::string outputName = "No output device";
    std::unique_ptr<Mixer> mixer;
    std::vector<ma_device_info> captureDevices;
    // Sources whose open failed. miniaudio's job thread can still touch them briefly after
    // a failed stream init returns, so they are freed only after the job thread is gone.
    std::vector<std::unique_ptr<FileSource>> failedOpens;

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
    impl_->mixer.reset(); // frees channels and sources while the resource manager still exists
    if (impl_->resourceManagerReady) {
        ma_resource_manager_uninit(&impl_->resourceManager); // joins the job thread
    }
    impl_->failedOpens.clear();
    if (impl_->contextReady) {
        ma_context_uninit(&impl_->context);
    }
}

bool AudioEngine::start(std::string* error)
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
        ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
        cfg.playback.format = ma_format_f32;
        cfg.playback.channels = 2;
        cfg.sampleRate = 0; // device native rate: no resampling on the output path
        cfg.periodSizeInMilliseconds = 10;
        cfg.performanceProfile = ma_performance_profile_low_latency;
        cfg.noPreSilencedOutputBuffer = MA_TRUE; // Mixer::process overwrites the whole buffer
        cfg.noClip = MA_TRUE;                    // Mixer already clamps
        cfg.dataCallback = &Impl::onPlayback;
        cfg.pUserData = &m;
        r = ma_device_init(&m.context, &cfg, &m.device);
        if (r == MA_SUCCESS) {
            m.deviceReady = true;
            sampleRate = m.device.sampleRate;
            m.outputName = m.device.playback.name;
        }
    }
    if (m.deviceReady) {
        sampleRate = m.device.sampleRate;
    }

    if (!m.resourceManagerReady) {
        ma_resource_manager_config rmc = ma_resource_manager_config_init();
        rmc.decodedFormat = ma_format_f32;
        rmc.decodedChannels = 2;
        rmc.decodedSampleRate = sampleRate;
        rmc.jobThreadCount = 1;
        m.resourceManagerReady = (ma_resource_manager_init(&rmc, &m.resourceManager) == MA_SUCCESS);
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

std::vector<std::string> AudioEngine::captureDeviceNames()
{
    std::vector<std::string> names;
    impl_->captureDevices.clear();
    if (!impl_->contextReady) {
        return names;
    }
    ma_device_info* playback = nullptr;
    ma_device_info* capture = nullptr;
    ma_uint32 playbackCount = 0;
    ma_uint32 captureCount = 0;
    if (ma_context_get_devices(&impl_->context, &playback, &playbackCount, &capture, &captureCount) != MA_SUCCESS) {
        return names;
    }
    impl_->captureDevices.assign(capture, capture + captureCount);
    for (const auto& d : impl_->captureDevices) {
        names.emplace_back(d.name);
    }
    return names;
}

std::unique_ptr<FileSource> AudioEngine::openFile(const std::string& utf8Path, std::string* error)
{
    if (!impl_->resourceManagerReady) {
        if (error) *error = "Audio file backend is not available";
        return nullptr;
    }
    std::error_code ec;
    const std::filesystem::path fsPath(std::u8string(reinterpret_cast<const char8_t*>(utf8Path.data()), utf8Path.size()));
    if (!std::filesystem::is_regular_file(fsPath, ec)) {
        if (error) *error = "File not found: " + utf8Path;
        return nullptr;
    }
    auto src = std::make_unique<FileSourceImpl>();
    if (!src->init(&impl_->resourceManager, utf8Path, error)) {
        impl_->failedOpens.push_back(std::move(src));
        return nullptr;
    }
    return src;
}

std::unique_ptr<InputSource> AudioEngine::openInput(const std::string& deviceName, std::string* error)
{
    if (!impl_->contextReady) {
        if (error) *error = "Audio backend is not available";
        return nullptr;
    }
    if (impl_->captureDevices.empty()) {
        captureDeviceNames();
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
    if (!src->init(&impl_->context, id, deviceName.empty() ? "Default input" : deviceName, sampleRate(), error)) {
        return nullptr;
    }
    return src;
}

std::unique_ptr<AppSource> AudioEngine::openApp(const std::string& exeName, const std::string& silentOutputId, std::string* error)
{
    return openAppCapture(exeName, sampleRate(), silentOutputId, error);
}

} // namespace psm
