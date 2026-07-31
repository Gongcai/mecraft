#pragma once

#include "../renderer/core/RenderSettings.h"
#include "../renderer/rhi/RhiTypes.h"

#include <nlohmann/json_fwd.hpp>

#include <optional>
#include <string>

namespace app {

struct RhiBackendSettingResult {
    bool isValid = true;
    std::optional<RhiBackend> backend;
};

struct VsyncSettingResult {
    bool isValid = true;
    std::optional<bool> enabled;
};

struct FullscreenSettingResult {
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
[[nodiscard]] nlohmann::json serializeRenderSettings(const RenderSettings& settings);
[[nodiscard]] bool deserializeRenderSettings(const nlohmann::json& value, RenderSettings& settings, std::string& error);
[[nodiscard]] RhiBackendSettingResult loadRhiBackend();
[[nodiscard]] VsyncSettingResult loadVsyncEnabled();
[[nodiscard]] FullscreenSettingResult loadFullscreenEnabled();

bool saveSettings(const AppSettingsData& settings);
bool saveRenderDistance(int renderDistance);
bool saveRenderSettings(const RenderSettings& settings);
bool saveRhiBackend(RhiBackend backend);
bool saveVsyncEnabled(bool enabled);
bool saveFullscreenEnabled(bool enabled);

} // namespace app
