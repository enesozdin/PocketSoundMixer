#include "Theme.h"

namespace psm {

namespace {

ThemeColors g_colors;

ImVec4 rgb(int r, int g, int b, float a = 1.0f)
{
    return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a);
}

// Swaps Dear ImGui's default blue for one accent color, keeping its alpha pattern.
void setAccent(ImGuiStyle& s, ImVec4 accent)
{
    ImVec4* c = s.Colors;
    const auto tint = [&](ImGuiCol idx, float alpha) { c[idx] = ImVec4(accent.x, accent.y, accent.z, alpha); };
    tint(ImGuiCol_Button, 0.45f);
    tint(ImGuiCol_ButtonHovered, 0.75f);
    tint(ImGuiCol_ButtonActive, 1.0f);
    tint(ImGuiCol_Header, 0.35f);
    tint(ImGuiCol_HeaderHovered, 0.6f);
    tint(ImGuiCol_HeaderActive, 0.85f);
    tint(ImGuiCol_SliderGrab, 0.85f);
    tint(ImGuiCol_SliderGrabActive, 1.0f);
    tint(ImGuiCol_CheckMark, 1.0f);
    tint(ImGuiCol_PlotLines, 1.0f);
    tint(ImGuiCol_TitleBgActive, 0.6f);
    tint(ImGuiCol_SeparatorHovered, 0.8f);
    tint(ImGuiCol_ResizeGripHovered, 0.7f);
}

} // namespace

void applyTheme(Theme theme, float uiScale)
{
    ImGuiStyle s;
    s.FrameRounding = 3.0f;
    s.GrabRounding = 3.0f;
    s.ChildRounding = 4.0f;
    s.WindowRounding = 4.0f;
    s.PopupRounding = 3.0f;

    switch (theme) {
    case Theme::Dark:
        ImGui::StyleColorsDark(&s);
        g_colors = {IM_COL32(30, 30, 34, 255), IM_COL32(80, 200, 120, 255), IM_COL32(230, 200, 60, 255),
                    IM_COL32(230, 70, 60, 255), rgb(255, 115, 102), rgb(255, 191, 77),
                    rgb(191, 64, 51), rgb(204, 166, 26)};
        break;
    case Theme::Midnight: {
        // Neutral near-black with a teal accent: easier on the eyes in a dark room.
        ImGui::StyleColorsDark(&s);
        ImVec4* c = s.Colors;
        c[ImGuiCol_WindowBg] = rgb(14, 15, 18);
        c[ImGuiCol_ChildBg] = rgb(19, 20, 24);
        c[ImGuiCol_PopupBg] = rgb(22, 23, 28, 0.98f);
        c[ImGuiCol_Border] = rgb(48, 50, 58);
        c[ImGuiCol_FrameBg] = rgb(32, 34, 40);
        c[ImGuiCol_FrameBgHovered] = rgb(44, 47, 55);
        c[ImGuiCol_FrameBgActive] = rgb(52, 56, 66);
        c[ImGuiCol_TitleBg] = rgb(19, 20, 24);
        c[ImGuiCol_Text] = rgb(224, 226, 232);
        c[ImGuiCol_TextDisabled] = rgb(128, 132, 145);
        setAccent(s, rgb(38, 166, 154));
        g_colors = {IM_COL32(26, 27, 32, 255), IM_COL32(38, 200, 150, 255), IM_COL32(240, 190, 70, 255),
                    IM_COL32(240, 80, 70, 255), rgb(255, 120, 110), rgb(255, 196, 90),
                    rgb(196, 70, 60), rgb(206, 160, 40)};
        break;
    }
    case Theme::Light: {
        ImGui::StyleColorsLight(&s);
        ImVec4* c = s.Colors;
        c[ImGuiCol_WindowBg] = rgb(240, 241, 244);
        c[ImGuiCol_ChildBg] = rgb(250, 250, 252);
        c[ImGuiCol_Border] = rgb(196, 199, 207);
        c[ImGuiCol_FrameBg] = rgb(222, 225, 232); // darker than the strip, so sliders stand out
        c[ImGuiCol_FrameBgHovered] = rgb(208, 214, 226);
        c[ImGuiCol_FrameBgActive] = rgb(196, 204, 220);
        c[ImGuiCol_TextDisabled] = rgb(110, 114, 124);
        setAccent(s, rgb(41, 98, 199));
        // Darker meter colors: the dark theme's bright yellow is unreadable on white.
        g_colors = {IM_COL32(214, 217, 224, 255), IM_COL32(40, 160, 90, 255), IM_COL32(214, 150, 20, 255),
                    IM_COL32(210, 50, 40, 255), rgb(190, 40, 30), rgb(170, 105, 0),
                    rgb(214, 80, 66), rgb(224, 176, 40)};
        break;
    }
    }
    s.ScaleAllSizes(uiScale);
    ImGui::GetStyle() = s;
}

const ThemeColors& themeColors() { return g_colors; }

const char* themeName(Theme theme)
{
    switch (theme) {
    case Theme::Midnight: return "Midnight";
    case Theme::Light: return "Light";
    case Theme::Dark: break;
    }
    return "Dark";
}

Theme themeFromName(const std::string& name)
{
    if (name == "Midnight") return Theme::Midnight;
    if (name == "Light") return Theme::Light;
    return Theme::Dark;
}

} // namespace psm
