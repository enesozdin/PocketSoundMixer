#pragma once

#include <functional>

namespace psm {

// Windows-only desktop integration: the notification-area (tray) icon, one running copy at a time,
// and starting with Windows. Elsewhere every call is a no-op and supported() is false.
class SystemTray {
public:
    struct Callbacks {
        std::function<void()> open;       // tray click, menu "Open", or a second copy was started
        std::function<void()> quit;       // menu "Quit"
        std::function<void()> endSession; // Windows is shutting down or signing out: save now
    };

    static bool supported();

    // False if another copy is already running; that copy is asked to show its window instead.
    static bool claimSingleInstance();

    // Registers the app in the user's Run key (started with `--tray`), or removes it.
    static void setStartWithWindows(bool enable);

    explicit SystemTray(Callbacks callbacks);
    ~SystemTray();
    SystemTray(const SystemTray&) = delete;
    SystemTray& operator=(const SystemTray&) = delete;

    void showIcon(bool show);

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace psm
