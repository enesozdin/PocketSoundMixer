#include "AudioEngine.h"
#include "MixerUI.h"
#include "Paths.h"
#include "Presets.h"
#include "Session.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include <cstdio>
#include <string>
#include <vector>

namespace {

struct DropState {
    psm::MixerUI* ui = nullptr;
};

void onDrop(GLFWwindow* window, int count, const char** paths)
{
    auto* state = static_cast<DropState*>(glfwGetWindowUserPointer(window));
    if (!state || !state->ui) return;
    double x = 0.0;
    double y = 0.0;
    glfwGetCursorPos(window, &x, &y);
    std::vector<std::string> files(paths, paths + count);
    state->ui->onFilesDropped(files, static_cast<float>(x), static_cast<float>(y));
}

void onGlfwError(int code, const char* text)
{
    std::fprintf(stderr, "GLFW error %d: %s\n", code, text);
}

} // namespace

int main()
{
    const auto configDir = psm::configDirectory();
    const auto presetFile = configDir / "presets.json";
    const auto sessionFile = configDir / "session.json";

    // Audio first: the Mixer's sample rate comes from the output device.
    psm::AudioEngine engine;
    std::string audioError;
    engine.start(&audioError);

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
    GLFWwindow* window = glfwCreateWindow(1440, 760, "PocketSoundMixer", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // vsync caps redraws at the display rate

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // layout is fixed; nothing to persist
    ImGui::StyleColorsDark();

    float scale = 1.0f;
    glfwGetWindowContentScale(window, &scale, nullptr);
#if defined(__APPLE__)
    scale = 1.0f; // macOS scales the framebuffer itself
#endif
    ImGui::GetStyle().ScaleAllSizes(scale);
    ImFontConfig fontCfg;
    fontCfg.SizePixels = 14.0f * scale;
    io.Fonts->AddFontDefault(&fontCfg);

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glslVersion);

    psm::MixerUI ui(engine, presets, presetFile);
    psm::SessionConfig session;
    std::string sessionError;
    if (!psm::loadSession(sessionFile, session, &sessionError)) {
        session = psm::defaultSession();
    }
    ui.applySession(session);
    if (!audioError.empty()) ui.setStatus(audioError);
    else if (!presetError.empty()) ui.setStatus(presetError);
    else if (!sessionError.empty()) ui.setStatus(sessionError);

    DropState dropState{&ui};
    glfwSetWindowUserPointer(window, &dropState);
    glfwSetDropCallback(window, onDrop);

    double lastTime = glfwGetTime();
    while (!glfwWindowShouldClose(window)) {
        // Sleep until input arrives. While meters move, wake at ~30 fps; when idle, rarely.
        // Rendering costs nothing while the user isn't looking at moving audio.
        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED)) {
            glfwWaitEventsTimeout(0.5);
            engine.mixer().collectGarbage(); // nothing is rendered while minimized
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
        glClearColor(0.08f, 0.08f, 0.09f, 1.0f);
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
