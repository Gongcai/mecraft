#ifndef MECRAFT_RENDER_SETTINGS_IMGUI_H
#define MECRAFT_RENDER_SETTINGS_IMGUI_H

struct RenderSettings;

namespace render_settings_imgui {

/// Draws the deferred renderer output visualization selector.
/// @param settings Renderer settings updated when the selected mode changes.
/// @return True when the user changed the selected debug view.
[[nodiscard]] bool showDeferredDebugView(RenderSettings& settings);

/// Draws all shadow controls shared by renderer configuration panels.
/// @param settings Renderer settings updated by the controls.
/// @return True when the user changed at least one shadow setting.
[[nodiscard]] bool showShadowSettings(RenderSettings& settings);

/// Draws all volumetric light and fog controls.
/// @param settings Renderer settings updated by the controls.
/// @return True when the user changed at least one volumetric setting.
[[nodiscard]] bool showVolumetricSettings(RenderSettings& settings);

/// Draws all screen-space ambient occlusion controls.
/// @param settings Renderer settings updated by the controls.
/// @return True when the user changed at least one SSAO setting.
[[nodiscard]] bool showSsaoSettings(RenderSettings& settings);

/// Draws all screen-space global illumination controls.
/// @param settings Renderer settings updated by the controls.
/// @return True when the user changed at least one SSGI setting.
[[nodiscard]] bool showSsgiSettings(RenderSettings& settings);

/// Draws the common exposure and color adjustment controls.
/// @param settings Renderer settings updated by the controls.
/// @return True when the user changed at least one adjustment.
[[nodiscard]] bool showPictureAdjustments(RenderSettings& settings);

/// Draws the complete post-process and picture adjustment controls.
/// @param settings Renderer settings updated by the controls.
/// @return True when the user changed at least one post-process setting.
[[nodiscard]] bool showPostProcessSettings(RenderSettings& settings);

} // namespace render_settings_imgui

#endif // MECRAFT_RENDER_SETTINGS_IMGUI_H
