#include "AudioEngine.h"
#include "MixerUI.h"
#include "Paths.h"
#include "Presets.h"
#include "AppIcon.h"
#include "Session.h"
#include "SystemTray.h"
#include "TurkishGlyphs.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr int kMinWindowWidth = 620; // logical pixels
constexpr int kMinWindowHeight = 480;

void onGlfwError(int code, const char* text)
{
    std::fprintf(stderr, "GLFW error %d: %s\n", code, text);
}

} // namespace

int main(int argc, char** argv)
{
    // One copy at a time: starting it again just brings the running one to the front.
    if (!psm::SystemTray::claimSingleInstance()) return 0;
    bool launchedToTray = false; // "--tray": started with Windows, so start hidden in the tray
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--tray") == 0) launchedToTray = true;
    }

    const auto configDir = psm::configDirectory();
    const auto presetFile = configDir / "presets.json";
    const auto sessionFile = configDir / "session.json";

    psm::SessionConfig session;
    std::string sessionError;
    if (!psm::loadSession(sessionFile, session, &sessionError)) {
        session = psm::defaultSession();
    }

    // Audio first: the Mixer's sample rate comes from the output device.
    psm::AudioEngine engine;
    std::string audioError;
    engine.start(session.outputDevice, &audioError);

    psm::PresetLibrary presets;
    std::string presetError;
    presets.loadUserPresets(presetFile, &presetError);

    glfwSetErrorCallback(onGlfwError);
    if (!glfwInit()) {
        return 1;
    }
#if defined(__APPLE__)
    const char* glslVersion = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
    const char* glslVersion = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    const bool useTray = psm::SystemTray::supported() && session.trayEnabled;
    glfwWindowHint(GLFW_VISIBLE, launchedToTray && useTray ? GLFW_FALSE : GLFW_TRUE);
    GLFWwindow* window = glfwCreateWindow(1440, 760, "PocketSoundMixer", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    // Smallest size that still shows the button row on one line and one full channel; the window can't
    // be shrunk past it. GLFW_SCALE_TO_MONITOR sizes the window in pixels, so scale the limit too.
    {
        float minScale = 1.0f;
        glfwGetWindowContentScale(window, &minScale, nullptr);
#if defined(__APPLE__)
        minScale = 1.0f; // macOS sizes windows in points
#endif
        glfwSetWindowSizeLimits(window, static_cast<int>(kMinWindowWidth * minScale), static_cast<int>(kMinWindowHeight * minScale),
                                GLFW_DONT_CARE, GLFW_DONT_CARE);
    }
    {
        // Window and taskbar icon (GLFW ignores this on macOS, which uses the app bundle's icon).
        std::vector<unsigned char> small = psm::makeAppIcon(32);
        std::vector<unsigned char> large = psm::makeAppIcon(64);
        const GLFWimage icons[2] = {{32, 32, small.data()}, {64, 64, large.data()}};
        glfwSetWindowIcon(window, 2, icons);
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // vsync caps redraws at the display rate

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // layout is fixed; nothing to persist
    float scale = 1.0f;
    glfwGetWindowContentScale(window, &scale, nullptr);
#if defined(__APPLE__)
    scale = 1.0f; // macOS scales the framebuffer itself
#endif
    ImFontConfig fontCfg;
    fontCfg.SizePixels = 14.0f * scale;
    io.Fonts->AddFontDefault(&fontCfg);
    // The default font has no Ğ ğ İ ı Ş ş; merge just those six glyphs in for the Turkish UI.
    static const ImWchar kTurkishRanges[] = {0x011E, 0x011F, 0x0130, 0x0131, 0x015E, 0x015F, 0};
    ImFontConfig trCfg;
    trCfg.MergeMode = true;
    trCfg.FontDataOwnedByAtlas = false; // static data, the atlas must not free it
    trCfg.PixelSnapH = true;
    io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(psm::kTurkishGlyphsTtf), sizeof(psm::kTurkishGlyphsTtf),
                                   13.0f * scale, &trCfg, kTurkishRanges);

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glslVersion);

    // Windows wakes the UI when the device volume changes (headset buttons, taskbar slider).
    psm::DeviceVolume deviceVolume([] { glfwPostEmptyEvent(); });
    psm::MixerUI ui(engine, presets, presetFile);
    ui.setDeviceVolume(&deviceVolume);
    ui.setUiScale(scale); // the theme scales the style; applySession applies the saved theme
    ui.applySession(session);
    // Tray, autostart and shutdown handling (Windows). Its callbacks run inside glfwWaitEvents*,
    // on this thread, so they can touch the window and UI directly.
    bool quitRequested = false;
    const auto showWindow = [window] {
        glfwShowWindow(window);
        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED)) glfwRestoreWindow(window);
        glfwFocusWindow(window);
    };
    psm::SystemTray tray({
        showWindow,
        [&quitRequested] { quitRequested = true; },
        [&] {
            psm::saveSession(sessionFile, ui.captureSession(), nullptr);
            presets.saveUserPresets(presetFile, nullptr);
            ui.releaseApps();
            engine.stop();
            quitRequested = true; // in case Windows lets us run on
        },
    });
    if (psm::SystemTray::supported()) {
        tray.showIcon(session.trayEnabled);
        psm::SystemTray::setStartWithWindows(session.startWithWindows);
        ui.setSystemHooks([&tray](bool on) { tray.showIcon(on); },
                          [](bool on) { psm::SystemTray::setStartWithWindows(on); });
    }
    if (launchedToTray && !useTray) showWindow();
    // With the tray on, closing the window only hides it; the mix keeps playing.
    struct CloseContext {
        psm::MixerUI* ui;
        const std::filesystem::path* sessionFile;
    } closeContext{&ui, &sessionFile};
    glfwSetWindowUserPointer(window, &closeContext);
    glfwSetWindowCloseCallback(window, [](GLFWwindow* w) {
        auto* ctx = static_cast<CloseContext*>(glfwGetWindowUserPointer(w));
        if (!psm::SystemTray::supported() || !ctx->ui->trayEnabled()) return;
        glfwSetWindowShouldClose(w, GLFW_FALSE);
        glfwHideWindow(w);
        psm::saveSession(*ctx->sessionFile, ctx->ui->captureSession(), nullptr); // it may run for days now
    });

    if (!audioError.empty()) ui.setStatus(audioError);
    else if (!presetError.empty()) ui.setStatus(presetError);
    else if (!sessionError.empty()) ui.setStatus(sessionError);

    double lastTime = glfwGetTime();
    while (!glfwWindowShouldClose(window) && !quitRequested) {
        // Sleep until input arrives. While meters move, wake at ~30 fps; when idle, rarely.
        // Rendering costs nothing while the user isn't looking at moving audio.
        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) || !glfwGetWindowAttrib(window, GLFW_VISIBLE)) {
            glfwWaitEventsTimeout(0.5);
            engine.mixer().collectGarbage(); // nothing is rendered while minimized or in the tray
            lastTime = glfwGetTime();
            continue;
        }
        glfwWaitEventsTimeout(ui.isAnimating() ? 1.0 / 30.0 : 0.5);

        const double now = glfwGetTime();
        const float dt = static_cast<float>(now - lastTime);
        lastTime = now;

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ui.draw(dt);
        ImGui::Render();

        int w = 0;
        int h = 0;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        const ImVec4 bg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
        glClearColor(bg.x, bg.y, bg.z, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    psm::saveSession(sessionFile, ui.captureSession(), nullptr);
    presets.saveUserPresets(presetFile, nullptr);

    engine.stop();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
