#pragma once

#include "../renderer/core/RenderSettings.h"
#include "../renderer/rhi/RhiTypes.h"

#include <optional>

namespace app {

struct RhiBackendSettingResult {
    bool isValid = true;
    std::optional<RhiBackend> backend;
};

struct VsyncSettingResult {
    bool isValid = true;
    std::optional<bool> enabled;
};

struct AppSettingsData {
    int renderDistance = 16;
    RenderSettings renderSettings;
};

[[nodiscard]] AppSettingsData loadSettings();
[[nodiscard]] int loadRenderDistance();
[[nodiscard]] RenderSettings loadRenderSettings(const RenderSettings& fallback = RenderSettings{});
[[nodiscard]] RhiBackendSettingResult loadRhiBackend();
[[nodiscard]] VsyncSettingResult loadVsyncEnabled();

bool saveSettings(const AppSettingsData& settings);
bool saveRenderDistance(int renderDistance);
bool saveRenderSettings(const RenderSettings& settings);
bool saveRhiBackend(RhiBackend backend);
bool saveVsyncEnabled(bool enabled);

} // namespace app
