#include "OpenUrl.h"

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#else
#include <cstdlib>
#include <string>
#endif

namespace psm {

void openUrl(const char* url)
{
    // Only called with fixed links from the string table, never with user text.
#if defined(_WIN32)
    ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
    const std::string cmd = std::string("open '") + url + "' &";
    if (std::system(cmd.c_str()) != 0) {
        // Nothing to do: there is no browser to fall back to.
    }
#else
    const std::string cmd = std::string("xdg-open '") + url + "' >/dev/null 2>&1 &";
    if (std::system(cmd.c_str()) != 0) {
        // Nothing to do: there is no browser to fall back to.
    }
#endif
}

} // namespace psm
