#ifndef MECRAFT_MODEL_SCENE_DEFERRED_RENDERER_H
#define MECRAFT_MODEL_SCENE_DEFERRED_RENDERER_H

#include <cstdint>
#include <memory>
#include <string>

#include <glm/glm.hpp>

class IDeferredGeometryProvider;
class ImGuiRhiRenderer;
class ResourceMgr;
class RhiCommandListPool;
class RhiDevice;
struct RenderSettings;

/// Owns the shared deferred environment used by the standalone model scene.
class ModelSceneDeferredRenderer {
public:
    ModelSceneDeferredRenderer();
    ~ModelSceneDeferredRenderer();

    ModelSceneDeferredRenderer(const ModelSceneDeferredRenderer&) = delete;
    ModelSceneDeferredRenderer& operator=(
        const ModelSceneDeferredRenderer&) = delete;

    /// Initializes deferred targets, atmosphere, shadows, and post-processing.
    [[nodiscard]] bool init(ResourceMgr& resourceMgr,
                            RhiDevice& rhiDevice,
                            RhiCommandListPool& commandListPool,
                            ImGuiRhiRenderer& imguiRenderer,
                            IDeferredGeometryProvider& geometryProvider);
    void shutdown();

    /// Allocates render targets and a stable ImGui texture binding for the viewport.
    [[nodiscard]] bool ensureViewport(uint32_t width, uint32_t height);

    /// Renders one deferred scene frame and composites it into the viewport texture.
    [[nodiscard]] bool render(const glm::mat4& view,
                              const glm::mat4& projection,
                              const glm::vec3& cameraPosition,
                              float deltaTime);

    /// Changes the standalone environment time and invalidates temporal history.
    /// @param timeOfDaySeconds Time within the 1200-second world day.
    void setTimeOfDay(float timeOfDaySeconds);

    /// Returns the current environment time within the 1200-second world day.
    [[nodiscard]] float timeOfDay() const;

    /// Replaces the standalone renderer configuration and resets temporal history.
    /// @param settings Complete deferred renderer configuration for subsequent frames.
    void setSettings(const RenderSettings& settings);

    /// Returns the renderer configuration supported by a new standalone scene.
    [[nodiscard]] static RenderSettings defaultSettings();

    /// Verifies that settings do not require gameplay-only rendering resources.
    [[nodiscard]] static bool validateSettings(const RenderSettings& settings,
                                               std::string& error);

    /// Returns the active standalone renderer configuration.
    [[nodiscard]] const RenderSettings& settings() const;

    [[nodiscard]] uint64_t viewportTextureId() const;
    [[nodiscard]] uint32_t viewportWidth() const;
    [[nodiscard]] uint32_t viewportHeight() const;
    [[nodiscard]] const std::string& lastError() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

#endif // MECRAFT_MODEL_SCENE_DEFERRED_RENDERER_H
