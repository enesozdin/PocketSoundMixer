#pragma once

#include "imgui.h"

#include <string>

namespace psm {

enum class Theme { Dark, Midnight, Light, Graphite, Violet }; // saved by name, so order is free

// Colors the UI draws itself (meters, warnings) that Dear ImGui's style does not cover.
struct ThemeColors {
    ImU32 meterBack;
    ImU32 meterNormal;
    ImU32 meterLoud;
    ImU32 meterLimit;
    ImVec4 errorText;
    ImVec4 warningText;
    ImVec4 muteOn;
    ImVec4 soloOn;
};

// Rebuilds the Dear ImGui style for `theme` at the given DPI scale. Cheap, but only call it on change.
void applyTheme(Theme theme, float uiScale);
const ThemeColors& themeColors();

const char* themeName(Theme theme); // English key saved in the session; see themeLabel() in the UI
Theme themeFromName(const std::string& name); // unknown names give Dark

} // namespace psm
