#pragma once

#include "UITheme.h"

namespace UIThemePresets {
    // Load built-in presets from assets/themes/*.json.
    // Falls back to hardcoded defaults if the file is missing or invalid.
    UITheme dark();
    UITheme light();

    // Load a theme by filename (without path) from the themes directory.
    // e.g. loadTheme("dark.json") loads from assets/themes/dark.json
    UITheme loadTheme(const std::string& filename);
}
