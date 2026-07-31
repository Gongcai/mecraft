#include "UIThemePresets.h"

#include <string>

#include "../../Paths.h"

namespace UIThemePresets {

static std::string themesPath(const char* filename) {
    return std::string(THEMES_DIR) + "/" + filename;
}

UITheme dark() {
    UITheme t;
    t.loadFromFile(themesPath("dark.json"));
    return t;
}

UITheme light() {
    UITheme t;
    t.loadFromFile(themesPath("light.json"));
    return t;
}

UITheme loadTheme(const std::string& filename) {
    UITheme t;
    t.loadFromFile(themesPath(filename.c_str()));
    return t;
}

} // namespace UIThemePresets
