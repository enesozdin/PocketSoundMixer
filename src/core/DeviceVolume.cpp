#include "DeviceVolume.h"

#if !defined(_WIN32)

namespace psm {

struct DeviceVolume::Impl {};

DeviceVolume::DeviceVolume(std::function<void()> onChange) : onChange_(std::move(onChange)) {}
DeviceVolume::~DeviceVolume() = default;
bool DeviceVolume::attach(const std::string&) { return false; }
void DeviceVolume::detach() {}
void DeviceVolume::update() {}
void DeviceVolume::setLevel(float) {}
void DeviceVolume::setMuted(bool) {}

} // namespace psm

#else

#include <windows.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <algorithm>

using Microsoft::WRL::ComPtr;

namespace psm {

namespace {

// PKEY_Device_FriendlyName, spelled out so we don't depend on INITGUID include order.
const PROPERTYKEY kFriendlyName = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};

// Tags our own volume changes, so the callback can tell them from the user's headset buttons.
const GUID kOwnChange = {0x5d0f3a2c, 0x7b1e, 0x4c55, {0x9a, 0x61, 0x2e, 0x8d, 0x41, 0x0b, 0x73, 0xc9}};

std::wstring toWide(const std::string& s)
{
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

// Minimal free-threaded COM object: Windows calls these from its own threads.
template <typename Interface>
class Callback : public Interface {
public:
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG n = --refs_;
        if (n == 0) delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override
    {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(Interface)) {
            *out = static_cast<Interface*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }

protected:
    virtual ~Callback() = default;

private:
    std::atomic<ULONG> refs_{1};
};

class VolumeCallback final : public Callback<IAudioEndpointVolumeCallback> {
public:
    VolumeCallback(std::atomic<float>& level, std::atomic<bool>& muted, const std::function<void()>& onChange)
        : level_(level), muted_(muted), onChange_(onChange) {}

    HRESULT STDMETHODCALLTYPE OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA data) override
    {
        if (!data) return S_OK;
        level_.store(data->fMasterVolume, std::memory_order_relaxed);
        muted_.store(data->bMuted != FALSE, std::memory_order_relaxed);
        if (data->guidEventContext != kOwnChange && onChange_) onChange_();
        return S_OK;
    }

private:
    std::atomic<float>& level_;
    std::atomic<bool>& muted_;
    const std::function<void()>& onChange_;
};

class DeviceCallback final : public Callback<IMMNotificationClient> {
public:
    DeviceCallback(std::atomic<bool>& defaultChanged, const std::function<void()>& onChange)
        : defaultChanged_(defaultChanged), onChange_(onChange) {}

    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override
    {
        if (flow == eRender && role == eConsole) {
            defaultChanged_.store(true, std::memory_order_relaxed);
            if (onChange_) onChange_();
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }

private:
    std::atomic<bool>& defaultChanged_;
    const std::function<void()>& onChange_;
};

ComPtr<IMMDevice> findRenderDevice(IMMDeviceEnumerator* enumerator, const std::wstring& name)
{
    ComPtr<IMMDevice> device;
    if (name.empty()) {
        enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
        return device;
    }
    ComPtr<IMMDeviceCollection> devices;
    if (FAILED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices))) return nullptr;
    UINT count = 0;
    devices->GetCount(&count);
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> candidate;
        ComPtr<IPropertyStore> props;
        if (FAILED(devices->Item(i, &candidate)) || FAILED(candidate->OpenPropertyStore(STGM_READ, &props))) continue;
        PROPVARIANT value;
        PropVariantInit(&value);
        const bool match = SUCCEEDED(props->GetValue(kFriendlyName, &value)) && value.vt == VT_LPWSTR && name == value.pwszVal;
        PropVariantClear(&value);
        if (match) return candidate;
    }
    return nullptr;
}

} // namespace

struct DeviceVolume::Impl {
    HRESULT com = E_FAIL;
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IAudioEndpointVolume> endpoint;
    VolumeCallback* volumeCallback = nullptr;
    DeviceCallback* deviceCallback = nullptr;

    void release()
    {
        if (endpoint && volumeCallback) endpoint->UnregisterControlChangeNotify(volumeCallback);
        endpoint.Reset();
        if (volumeCallback) volumeCallback->Release();
        volumeCallback = nullptr;
    }
};

DeviceVolume::DeviceVolume(std::function<void()> onChange)
    : impl_(std::make_unique<Impl>())
    , onChange_(std::move(onChange))
{
    // Lives on the UI thread for the app's lifetime; any apartment works for these interfaces.
    impl_->com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&impl_->enumerator)))) {
        return;
    }
    impl_->deviceCallback = new DeviceCallback(defaultChanged_, onChange_);
    impl_->enumerator->RegisterEndpointNotificationCallback(impl_->deviceCallback);
}

DeviceVolume::~DeviceVolume()
{
    impl_->release();
    if (impl_->enumerator && impl_->deviceCallback) {
        impl_->enumerator->UnregisterEndpointNotificationCallback(impl_->deviceCallback);
    }
    if (impl_->deviceCallback) impl_->deviceCallback->Release();
    impl_->enumerator.Reset();
    if (SUCCEEDED(impl_->com)) CoUninitialize();
}

bool DeviceVolume::attach(const std::string& deviceName)
{
    impl_->release();
    attached_.store(false, std::memory_order_relaxed);
    deviceName_ = deviceName;
    defaultChanged_.store(false, std::memory_order_relaxed);
    if (!impl_->enumerator) return false;

    ComPtr<IMMDevice> device = findRenderDevice(impl_->enumerator.Get(), toWide(deviceName));
    if (!device || FAILED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, &impl_->endpoint))) {
        impl_->endpoint.Reset();
        return false;
    }
    float level = 1.0f;
    BOOL mute = FALSE;
    impl_->endpoint->GetMasterVolumeLevelScalar(&level);
    impl_->endpoint->GetMute(&mute);
    level_.store(level, std::memory_order_relaxed);
    muted_.store(mute != FALSE, std::memory_order_relaxed);
    impl_->volumeCallback = new VolumeCallback(level_, muted_, onChange_);
    impl_->endpoint->RegisterControlChangeNotify(impl_->volumeCallback);
    attached_.store(true, std::memory_order_relaxed);
    return true;
}

void DeviceVolume::detach()
{
    impl_->release();
    attached_.store(false, std::memory_order_relaxed);
}

void DeviceVolume::update()
{
    // Only "System default" follows the default device; a picked device stays picked.
    if (defaultChanged_.exchange(false, std::memory_order_relaxed) && deviceName_.empty()) {
        attach({});
    }
}

void DeviceVolume::setLevel(float level)
{
    if (!impl_->endpoint) return;
    level = std::clamp(level, 0.0f, 1.0f);
    level_.store(level, std::memory_order_relaxed);
    impl_->endpoint->SetMasterVolumeLevelScalar(level, &kOwnChange);
}

void DeviceVolume::setMuted(bool muted)
{
    if (!impl_->endpoint) return;
    muted_.store(muted, std::memory_order_relaxed);
    impl_->endpoint->SetMute(muted ? TRUE : FALSE, &kOwnChange);
}

} // namespace psm

#endif
