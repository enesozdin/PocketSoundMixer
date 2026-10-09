#include "Paths.h"

#include <cstdlib>

namespace psm {

std::filesystem::path configDirectory()
{
    constexpr const char* kAppFolder = "PocketSoundMixer";
#if defined(_WIN32)
    if (const wchar_t* appData = _wgetenv(L"APPDATA")) {
        return std::filesystem::path(appData) / kAppFolder;
    }
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME")) {
        return std::filesystem::path(home) / "Library" / "Application Support" / kAppFolder;
    }
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
        return std::filesystem::path(xdg) / kAppFolder;
    }
    if (const char* home = std::getenv("HOME")) {
        return std::filesystem::path(home) / ".config" / kAppFolder;
    }
#endif
    return std::filesystem::current_path() / kAppFolder;
}

} // namespace psm
