#include "AppCapture.h"

#if !defined(_WIN32)

namespace psm {

bool appCaptureSupported() { return false; }

std::vector<AudioAppInfo> listAudioApps() { return {}; }

std::vector<OutputDeviceInfo> listOutputDevices() { return {}; }

void resetAllAppOutputs() {}

std::string pickSilentOutputId(const std::vector<OutputDeviceInfo>&, const std::string&) { return {}; }

void openAppVolumeSettings() {}

std::unique_ptr<AppSource> openAppCapture(const std::string&, uint32_t, const std::string&, std::string* error)
{
    if (error) *error = "Per-app capture is only available on Windows for now";
    return nullptr;
}

} // namespace psm

#else

#include "StereoRingBuffer.h"

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <audiopolicy.h>
#include <avrt.h>
#include <inspectable.h>
#include <mmdeviceapi.h>
#include <roapi.h>
#include <winstring.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <wrl/client.h>
#include <wrl/implements.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cwchar>
#include <cwctype>
#include <map>
#include <mutex>
#include <thread>

#ifndef VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK
    #define VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK L"VAD\\Process_Loopback"
#endif

namespace psm {

namespace {

using Microsoft::WRL::ComPtr;

std::string toUtf8(const std::wstring& w)
{
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring toWide(const std::string& s)
{
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

bool equalsNoCase(const std::wstring& a, const std::wstring& b)
{
    return a.size() == b.size()
        && std::equal(a.begin(), a.end(), b.begin(), [](wchar_t x, wchar_t y) { return std::towlower(x) == std::towlower(y); });
}

struct ProcessEntry {
    DWORD parent = 0;
    std::wstring exe;
};

std::map<DWORD, ProcessEntry> snapshotProcesses()
{
    std::map<DWORD, ProcessEntry> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
        out[pe.th32ProcessID] = {pe.th32ParentProcessID, pe.szExeFile};
    }
    CloseHandle(snap);
    return out;
}

// Browsers, Discord and many games play audio from a child process with the same exe name.
// Capturing the top-most process of that name with INCLUDE_TARGET_PROCESS_TREE covers all of them.
DWORD findRootProcess(const std::wstring& exe)
{
    const auto procs = snapshotProcesses();
    for (const auto& [pid, entry] : procs) {
        if (!equalsNoCase(entry.exe, exe)) continue;
        const auto parent = procs.find(entry.parent);
        if (parent == procs.end() || !equalsNoCase(parent->second.exe, exe)) {
            return pid;
        }
    }
    return 0;
}

// Scoped COM init that tolerates a thread already initialized in another mode.
struct ComScope {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~ComScope()
    {
        if (SUCCEEDED(hr)) CoUninitialize();
    }
};

class ActivationHandler
    : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
                                          Microsoft::WRL::FtmBase, IActivateAudioInterfaceCompletionHandler> {
public:
    ActivationHandler() { done_ = CreateEventW(nullptr, TRUE, FALSE, nullptr); }
    ~ActivationHandler() { CloseHandle(done_); }

    STDMETHOD(ActivateCompleted)(IActivateAudioInterfaceAsyncOperation* op) override
    {
        HRESULT activateHr = E_FAIL;
        ComPtr<IUnknown> unknown;
        result_ = op->GetActivateResult(&activateHr, &unknown);
        if (SUCCEEDED(result_)) result_ = activateHr;
        if (SUCCEEDED(result_)) result_ = unknown.As(&client_);
        SetEvent(done_);
        return S_OK;
    }

    HRESULT wait(DWORD timeoutMs, ComPtr<IAudioClient>& client)
    {
        if (WaitForSingleObject(done_, timeoutMs) != WAIT_OBJECT_0) return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        client = client_;
        return result_;
    }

private:
    HANDLE done_ = nullptr;
    HRESULT result_ = E_FAIL;
    ComPtr<IAudioClient> client_;
};

std::string hresultText(const char* what, HRESULT hr)
{
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%s failed (0x%08lX)", what, static_cast<unsigned long>(hr));
    return buf;
}


// ---------------------------------------------------------------------------------------------
// Per-app output routing through Windows' AudioPolicyConfig. This is what the Settings page
// "App volume and device preferences" uses. It is undocumented but has kept the same layout
// since Windows 10 1803 (EarTrumpet relies on it too); only the interface id changed in 21H2.
struct IAudioPolicyConfigFactory : public IInspectable {
    // 19 methods we never call, kept only for the vtable layout.
    virtual HRESULT STDMETHODCALLTYPE Unused00() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused01() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused02() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused03() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused04() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused05() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused06() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused07() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused08() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused09() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused10() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused11() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused12() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused13() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused14() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused15() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused16() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused17() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused18() = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPersistedDefaultAudioEndpoint(UINT32 processId, EDataFlow flow, ERole role, HSTRING deviceId) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPersistedDefaultAudioEndpoint(UINT32 processId, EDataFlow flow, ERole role, HSTRING* deviceId) = 0;
    virtual HRESULT STDMETHODCALLTYPE ClearAllPersistedApplicationDefaultEndpoints() = 0;
};

constexpr wchar_t kMmDevApiToken[] = L"\\\\?\\SWD#MMDEVAPI#";
constexpr wchar_t kRenderInterface[] = L"#{e6327cad-dcec-4949-ae8a-991e976a79d2}";

DWORD windowsBuildNumber()
{
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    OSVERSIONINFOW info{};
    info.dwOSVersionInfoSize = sizeof(info);
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
        if (auto fn = reinterpret_cast<RtlGetVersionFn>(reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")))) {
            fn(&info);
        }
    }
    return info.dwBuildNumber;
}

ComPtr<IAudioPolicyConfigFactory> createPolicyConfig()
{
    static const IID kIid21H2 = {0xab3d4648, 0xe242, 0x459f, {0xb0, 0x2f, 0x54, 0x1c, 0x70, 0x30, 0x63, 0x24}};
    static const IID kIidDownlevel = {0x2a59116d, 0x6c4f, 0x45e0, {0xa7, 0x4f, 0x70, 0x7e, 0x3f, 0xef, 0x92, 0x58}};
    const wchar_t className[] = L"Windows.Media.Internal.AudioPolicyConfig";
    HSTRING_HEADER header{};
    HSTRING name = nullptr;
    ComPtr<IAudioPolicyConfigFactory> factory;
    if (SUCCEEDED(WindowsCreateStringReference(className, static_cast<UINT32>(wcslen(className)), &header, &name))) {
        const IID& iid = windowsBuildNumber() >= 21390 ? kIid21H2 : kIidDownlevel;
        RoGetActivationFactory(name, iid, reinterpret_cast<void**>(factory.GetAddressOf()));
    }
    return factory;
}

std::vector<DWORD> processesNamed(const std::wstring& exe)
{
    std::vector<DWORD> pids;
    for (const auto& [pid, entry] : snapshotProcesses()) {
        if (equalsNoCase(entry.exe, exe)) pids.push_back(pid);
    }
    return pids;
}

// Moves every process of one app to another output and remembers where each one was,
// so restore() puts things back exactly as the user had them.
class AppRouter {
public:
    void route(const std::wstring& exe, const std::wstring& endpointId)
    {
        if (endpointId.empty()) return;
        if (!policy_) policy_ = createPolicyConfig();
        if (!policy_) return;
        const std::wstring full = std::wstring(kMmDevApiToken) + endpointId + kRenderInterface;
        HSTRING target = nullptr;
        if (FAILED(WindowsCreateString(full.c_str(), static_cast<UINT32>(full.size()), &target))) return;
        for (DWORD pid : processesNamed(exe)) {
            if (previous_.count(pid)) continue; // already moved
            HSTRING before = nullptr;
            policy_->GetPersistedDefaultAudioEndpoint(pid, eRender, eMultimedia, &before);
            if (before && full == WindowsGetStringRawBuffer(before, nullptr)) {
                // Still parked from an earlier run (the app closed while captured):
                // its real setting was the system default.
                WindowsDeleteString(before);
                before = nullptr;
            }
            previous_[pid] = before; // null: the app follows the system default
            policy_->SetPersistedDefaultAudioEndpoint(pid, eRender, eMultimedia, target);
            policy_->SetPersistedDefaultAudioEndpoint(pid, eRender, eConsole, target);
        }
        WindowsDeleteString(target);
    }

    void restore()
    {
        for (auto& [pid, before] : previous_) {
            if (policy_) {
                policy_->SetPersistedDefaultAudioEndpoint(pid, eRender, eMultimedia, before);
                policy_->SetPersistedDefaultAudioEndpoint(pid, eRender, eConsole, before);
            }
            if (before) WindowsDeleteString(before);
        }
        previous_.clear();
    }

    bool active() const { return !previous_.empty(); }

private:
    ComPtr<IAudioPolicyConfigFactory> policy_;
    std::map<DWORD, HSTRING> previous_;
};

// PKEY_Device_FriendlyName, spelled out so we don't depend on INITGUID include order.
const PROPERTYKEY kDeviceFriendlyName = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};

std::wstring endpointIdOf(IMMDevice* device)
{
    LPWSTR id = nullptr;
    std::wstring out;
    if (SUCCEEDED(device->GetId(&id)) && id) {
        out = id;
        CoTaskMemFree(id);
    }
    return out;
}

// ---------------------------------------------------------------------------------------------
class WasapiAppSource final : public AppSource {
public:
    WasapiAppSource(std::string exeName, uint32_t sampleRate, std::string silentOutputId)
        : exeName_(std::move(exeName))
        , exeWide_(toWide(exeName_))
        , silentOutputId_(toWide(silentOutputId))
        , sampleRate_(sampleRate)
        , ring_(sampleRate / 4) // 250 ms of headroom
        , targetLatency_(sampleRate / 50) // 20 ms
        , maxLatency_(sampleRate / 16)    // ~60 ms
    {
        stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        thread_ = std::thread([this] { run(); });
    }

    ~WasapiAppSource() override
    {
        SetEvent(stopEvent_);
        if (thread_.joinable()) thread_.join();
        CloseHandle(stopEvent_);
    }

    void read(float* out, uint32_t frames) override
    {
        ring_.read(out, frames, targetLatency_, maxLatency_);
    }

    const std::string& exeName() const override { return exeName_; }
    State state() const override { return state_.load(std::memory_order_relaxed); }
    std::string lastError() const override
    {
        std::lock_guard<std::mutex> lock(errorMutex_);
        return error_;
    }
    bool isRerouted() const override { return rerouted_.load(std::memory_order_relaxed); }

private:
    void setError(std::string text)
    {
        std::lock_guard<std::mutex> lock(errorMutex_);
        error_ = std::move(text);
    }

    void run()
    {
        ComScope com;
        DWORD taskIndex = 0;
        HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Audio", &taskIndex); // OS-level audio priority
        while (WaitForSingleObject(stopEvent_, 0) != WAIT_OBJECT_0) {
            const DWORD pid = findRootProcess(exeWide_);
            if (pid != 0) {
                capture(pid);
            } else {
                state_ = State::WaitingForApp;
            }
            // Poll for the app (re)starting. Cheap: one process snapshot per second.
            if (WaitForSingleObject(stopEvent_, 1000) == WAIT_OBJECT_0) break;
        }
        router_.restore(); // the app plays on its own output again once the channel is gone
        rerouted_ = false;
        if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
    }

    void capture(DWORD pid)
    {
        AUDIOCLIENT_ACTIVATION_PARAMS params{};
        params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
        params.ProcessLoopbackParams.TargetProcessId = pid;
        params.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
        PROPVARIANT pv{};
        pv.vt = VT_BLOB;
        pv.blob.cbSize = sizeof(params);
        pv.blob.pBlobData = reinterpret_cast<BYTE*>(&params);

        auto handler = Microsoft::WRL::Make<ActivationHandler>();
        ComPtr<IActivateAudioInterfaceAsyncOperation> op;
        HRESULT hr = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient), &pv,
                                                 handler.Get(), &op);
        ComPtr<IAudioClient> client;
        if (SUCCEEDED(hr)) hr = handler->wait(5000, client);
        if (FAILED(hr)) {
            fail(hresultText("Starting app capture", hr) + ". Needs Windows 10 build 20348+ or Windows 11.");
            return;
        }

        // Process loopback has no mix format of its own; ask for the engine's format directly
        // so Windows does the conversion and the audio thread only copies.
        WAVEFORMATEX wf{};
        wf.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        wf.nChannels = 2;
        wf.nSamplesPerSec = sampleRate_;
        wf.wBitsPerSample = 32;
        wf.nBlockAlign = 8;
        wf.nAvgBytesPerSec = sampleRate_ * 8;
        const DWORD flags = AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK
                          | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 200000 /* 20 ms */, 0, &wf, nullptr);
        if (FAILED(hr)) {
            fail(hresultText("IAudioClient::Initialize", hr));
            return;
        }
        HANDLE packetEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        ComPtr<IAudioCaptureClient> captureClient;
        hr = client->SetEventHandle(packetEvent);
        if (SUCCEEDED(hr)) hr = client->GetService(IID_PPV_ARGS(&captureClient));
        if (SUCCEEDED(hr)) hr = client->Start();
        if (FAILED(hr)) {
            CloseHandle(packetEvent);
            fail(hresultText("Starting capture stream", hr));
            return;
        }
        state_ = State::Capturing;
        setError({});
        // Only now that capture works: park the app's own output so it is heard once, via the mixer.
        router_.route(exeWide_, silentOutputId_);
        rerouted_ = router_.active();
        DWORD lastRouteCheck = GetTickCount();

        HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
        const HANDLE waits[3] = {stopEvent_, packetEvent, process};
        const DWORD waitCount = process ? 3 : 2;
        for (;;) {
            const DWORD r = WaitForMultipleObjects(waitCount, waits, FALSE, 1000);
            if (r == WAIT_OBJECT_0 || r == WAIT_OBJECT_0 + 2) {
                break; // stopping, or the app exited (we go back to waiting for it)
            }
            if (r == WAIT_FAILED) {
                break;
            }
            // Browsers and Discord start new audio processes on the fly; move those too.
            if (!silentOutputId_.empty() && GetTickCount() - lastRouteCheck > 2000) {
                router_.route(exeWide_, silentOutputId_);
                lastRouteCheck = GetTickCount();
            }
            UINT32 packetFrames = 0;
            while (SUCCEEDED(captureClient->GetNextPacketSize(&packetFrames)) && packetFrames > 0) {
                BYTE* data = nullptr;
                UINT32 frames = 0;
                DWORD bufferFlags = 0;
                if (FAILED(captureClient->GetBuffer(&data, &frames, &bufferFlags, nullptr, nullptr))) {
                    break;
                }
                if (bufferFlags & AUDCLNT_BUFFERFLAGS_SILENT) {
                    ring_.writeSilence(frames);
                } else {
                    ring_.write(reinterpret_cast<const float*>(data), frames);
                }
                captureClient->ReleaseBuffer(frames);
            }
        }
        client->Stop();
        if (process) CloseHandle(process);
        CloseHandle(packetEvent);
        state_ = State::WaitingForApp;
    }

    void fail(std::string text)
    {
        setError(std::move(text));
        state_ = State::Failed;
    }

    std::string exeName_;
    std::wstring exeWide_;
    std::wstring silentOutputId_;
    AppRouter router_; // capture thread only
    std::atomic<bool> rerouted_{false};
    uint32_t sampleRate_;
    StereoRingBuffer ring_;
    uint32_t targetLatency_;
    uint32_t maxLatency_;
    std::atomic<State> state_{State::WaitingForApp};
    mutable std::mutex errorMutex_;
    std::string error_;
    HANDLE stopEvent_ = nullptr;
    std::thread thread_;
};

} // namespace

bool appCaptureSupported() { return true; }

void openAppVolumeSettings()
{
    ShellExecuteW(nullptr, L"open", L"ms-settings:apps-volume", nullptr, nullptr, SW_SHOWNORMAL);
}

std::vector<AudioAppInfo> listAudioApps()
{
    std::vector<AudioAppInfo> apps;
    ComScope com;
    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)))) {
        return apps;
    }
    ComPtr<IMMDeviceCollection> devices;
    if (FAILED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices))) {
        return apps;
    }
    const auto procs = snapshotProcesses();
    const DWORD self = GetCurrentProcessId();
    UINT deviceCount = 0;
    devices->GetCount(&deviceCount);
    for (UINT d = 0; d < deviceCount; ++d) {
        ComPtr<IMMDevice> device;
        ComPtr<IAudioSessionManager2> manager;
        ComPtr<IAudioSessionEnumerator> sessions;
        if (FAILED(devices->Item(d, &device))
            || FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, &manager))
            || FAILED(manager->GetSessionEnumerator(&sessions))) {
            continue;
        }
        int sessionCount = 0;
        sessions->GetCount(&sessionCount);
        for (int i = 0; i < sessionCount; ++i) {
            ComPtr<IAudioSessionControl> control;
            ComPtr<IAudioSessionControl2> control2;
            if (FAILED(sessions->GetSession(i, &control)) || FAILED(control.As(&control2))) continue;
            if (control2->IsSystemSoundsSession() == S_OK) continue;
            DWORD pid = 0;
            if (FAILED(control2->GetProcessId(&pid)) || pid == 0 || pid == self) continue;
            const auto it = procs.find(pid);
            if (it == procs.end()) continue;
            const std::string exe = toUtf8(it->second.exe);
            if (std::any_of(apps.begin(), apps.end(), [&](const AudioAppInfo& a) { return equalsNoCase(toWide(a.exeName), it->second.exe); })) {
                continue;
            }
            const size_t dot = exe.find_last_of('.');
            apps.push_back({exe, dot == std::string::npos ? exe : exe.substr(0, dot)});
        }
    }
    std::sort(apps.begin(), apps.end(), [](const AudioAppInfo& a, const AudioAppInfo& b) { return a.displayName < b.displayName; });
    return apps;
}

void resetAllAppOutputs()
{
    ComScope com;
    if (auto policy = createPolicyConfig()) {
        policy->ClearAllPersistedApplicationDefaultEndpoints();
    }
}

std::vector<OutputDeviceInfo> listOutputDevices()
{
    std::vector<OutputDeviceInfo> out;
    ComScope com;
    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)))) {
        return out;
    }
    std::wstring defaultId;
    ComPtr<IMMDevice> defaultDevice;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &defaultDevice))) {
        defaultId = endpointIdOf(defaultDevice.Get());
    }
    ComPtr<IMMDeviceCollection> devices;
    if (FAILED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices))) {
        return out;
    }
    UINT count = 0;
    devices->GetCount(&count);
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> device;
        if (FAILED(devices->Item(i, &device))) continue;
        OutputDeviceInfo info;
        const std::wstring id = endpointIdOf(device.Get());
        info.id = toUtf8(id);
        info.isDefault = id == defaultId;
        ComPtr<IPropertyStore> props;
        PROPVARIANT name;
        PropVariantInit(&name);
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &props)) && SUCCEEDED(props->GetValue(kDeviceFriendlyName, &name))
            && name.vt == VT_LPWSTR) {
            info.name = toUtf8(name.pwszVal);
        }
        PropVariantClear(&name);
        if (info.name.empty()) info.name = info.id;
        out.push_back(std::move(info));
    }
    return out;
}

std::string pickSilentOutputId(const std::vector<OutputDeviceInfo>& devices, const std::string& mixerOutputName)
{
    for (const OutputDeviceInfo& d : devices) {
        if (!d.isDefault && d.name != mixerOutputName) return d.id;
    }
    return {};
}

std::unique_ptr<AppSource> openAppCapture(const std::string& exeName, uint32_t sampleRate,
                                          const std::string& silentOutputId, std::string* error)
{
    if (exeName.empty()) {
        if (error) *error = "No app selected";
        return nullptr;
    }
    return std::make_unique<WasapiAppSource>(exeName, sampleRate, silentOutputId);
}

} // namespace psm

#endif
