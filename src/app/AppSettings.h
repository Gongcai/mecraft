#pragma once

#include "../renderer/core/RenderSettings.h"

namespace app {

struct AppSettingsData {
    int renderDistance = 16;
    RenderSettings renderSettings;
};

[[nodiscard]] AppSettingsData loadSettings();
[[nodiscard]] int loadRenderDistance();
[[nodiscard]] RenderSettings loadRenderSettings(const RenderSettings& fallback = RenderSettings{});

bool saveSettings(const AppSettingsData& settings);
bool saveRenderDistance(int renderDistance);
bool saveRenderSettings(const RenderSettings& settings);

} // namespace app
