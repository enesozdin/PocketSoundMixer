#include "SystemTray.h"

#if !defined(_WIN32)

namespace psm {

struct SystemTray::Impl {};

bool SystemTray::supported() { return false; }
bool SystemTray::claimSingleInstance() { return true; }
void SystemTray::setStartWithWindows(bool) {}
SystemTray::SystemTray(Callbacks) {}
SystemTray::~SystemTray() = default;
void SystemTray::showIcon(bool) {}

} // namespace psm

#else

#include "AppIcon.h"
#include "Lang.h"

#include <windows.h>
#include <shellapi.h>

#include <string>
#include <vector>

namespace psm {

namespace {

const wchar_t kWindowClass[] = L"PocketSoundMixer.Tray";
const wchar_t kInstanceMutex[] = L"Local\\PocketSoundMixer.Instance";
const wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t kRunValue[] = L"PocketSoundMixer";
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kShowMessage = WM_APP + 2; // sent by a second copy
constexpr UINT kTrayId = 1;
constexpr UINT kMenuOpen = 1;
constexpr UINT kMenuQuit = 2;

std::wstring toWide(const char* s)
{
    const int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring w(static_cast<size_t>(n > 0 ? n - 1 : 0), L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s, -1, w.data(), n);
    return w;
}

HICON createIcon(int size)
{
    const std::vector<std::uint8_t> rgba = makeAppIcon(size);
    BITMAPV5HEADER bi = {};
    bi.bV5Size = sizeof(bi);
    bi.bV5Width = size;
    bi.bV5Height = -size; // top-down
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;
    void* bits = nullptr;
    HDC dc = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, dc);
    if (!color) return nullptr;
    auto* out = static_cast<std::uint8_t*>(bits);
    for (size_t i = 0; i < rgba.size(); i += 4) { // RGBA -> BGRA
        out[i] = rgba[i + 2];
        out[i + 1] = rgba[i + 1];
        out[i + 2] = rgba[i];
        out[i + 3] = rgba[i + 3];
    }
    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO info = {};
    info.fIcon = TRUE;
    info.hbmColor = color;
    info.hbmMask = mask;
    HICON icon = CreateIconIndirect(&info);
    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}

} // namespace

struct SystemTray::Impl {
    Callbacks callbacks;
    HWND window = nullptr;
    HICON icon = nullptr;
    UINT taskbarCreated = 0;
    bool iconShown = false;

    void addIcon()
    {
        NOTIFYICONDATAW nid = {};
        nid.cbSize = sizeof(nid);
        nid.hWnd = window;
        nid.uID = kTrayId;
        nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        nid.uCallbackMessage = kTrayMessage;
        nid.hIcon = icon;
        wcscpy_s(nid.szTip, L"PocketSoundMixer");
        Shell_NotifyIconW(NIM_ADD, &nid);
    }

    void removeIcon()
    {
        NOTIFYICONDATAW nid = {};
        nid.cbSize = sizeof(nid);
        nid.hWnd = window;
        nid.uID = kTrayId;
        Shell_NotifyIconW(NIM_DELETE, &nid);
    }

    void showMenu()
    {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, kMenuOpen, toWide(tr(S::TrayOpen)).c_str());
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kMenuQuit, toWide(tr(S::TrayQuit)).c_str());
        SetMenuDefaultItem(menu, kMenuOpen, FALSE);
        POINT pt;
        GetCursorPos(&pt);
        SetForegroundWindow(window); // or the menu won't close when clicking elsewhere
        const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, pt.x, pt.y, 0, window, nullptr);
        PostMessageW(window, WM_NULL, 0, 0);
        DestroyMenu(menu);
        if (cmd == kMenuOpen && callbacks.open) callbacks.open();
        if (cmd == kMenuQuit && callbacks.quit) callbacks.quit();
    }

    static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
        if (msg == kTrayMessage) {
            switch (LOWORD(lp)) {
            case WM_LBUTTONUP:
            case WM_LBUTTONDBLCLK:
                if (self->callbacks.open) self->callbacks.open();
                break;
            case WM_RBUTTONUP:
            case WM_CONTEXTMENU:
                self->showMenu();
                break;
            default:
                break;
            }
            return 0;
        }
        if (msg == kShowMessage) {
            if (self->callbacks.open) self->callbacks.open();
            return 0;
        }
        if (msg == self->taskbarCreated && self->iconShown) { // Explorer restarted: the icon is gone
            self->addIcon();
            return 0;
        }
        if (msg == WM_QUERYENDSESSION) return TRUE;
        if (msg == WM_ENDSESSION) {
            // The process ends right after this returns: save and put apps' outputs back now.
            if (wp && self->callbacks.endSession) self->callbacks.endSession();
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
};

bool SystemTray::supported() { return true; }

bool SystemTray::claimSingleInstance()
{
    // Kept for the whole process: Windows releases it when the app exits, however it exits.
    CreateMutexW(nullptr, FALSE, kInstanceMutex);
    if (GetLastError() != ERROR_ALREADY_EXISTS) return true;
    if (HWND other = FindWindowW(kWindowClass, nullptr)) {
        AllowSetForegroundWindow(ASFW_ANY); // lets the running copy bring its window to the front
        PostMessageW(other, kShowMessage, 0, 0);
    }
    return false;
}

void SystemTray::setStartWithWindows(bool enable)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return;
    if (enable) {
        // Rewritten on every launch, so it follows the exe if it is moved.
        wchar_t path[MAX_PATH] = {};
        const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
        if (n > 0 && n < MAX_PATH) {
            const std::wstring command = L"\"" + std::wstring(path) + L"\" --tray";
            RegSetValueExW(key, kRunValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                           static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
        }
    } else {
        RegDeleteValueW(key, kRunValue);
    }
    RegCloseKey(key);
}

SystemTray::SystemTray(Callbacks callbacks)
    : impl_(new Impl)
{
    impl_->callbacks = std::move(callbacks);
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &Impl::proc;
    wc.hInstance = instance;
    wc.lpszClassName = kWindowClass;
    RegisterClassExW(&wc);
    // A hidden top-level window (not message-only): only those get the shutdown messages.
    impl_->window = CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClass, L"PocketSoundMixer", WS_POPUP, 0, 0, 0, 0,
                                    nullptr, nullptr, instance, nullptr);
    if (impl_->window) SetWindowLongPtrW(impl_->window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(impl_));
    impl_->taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    impl_->icon = createIcon(GetSystemMetrics(SM_CXSMICON) > 16 ? 32 : 16);
}

SystemTray::~SystemTray()
{
    showIcon(false);
    if (impl_->window) DestroyWindow(impl_->window);
    if (impl_->icon) DestroyIcon(impl_->icon);
    delete impl_;
}

void SystemTray::showIcon(bool show)
{
    if (!impl_->window || show == impl_->iconShown) return;
    impl_->iconShown = show;
    if (show) impl_->addIcon();
    else impl_->removeIcon();
}

} // namespace psm

#endif
