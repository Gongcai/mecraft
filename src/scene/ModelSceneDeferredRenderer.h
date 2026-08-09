#ifndef MECRAFT_MODEL_SCENE_DEFERRED_RENDERER_H
#define MECRAFT_MODEL_SCENE_DEFERRED_RENDERER_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "renderer/core/FrameContext.h"
#include "renderer/passes/ReflectionProbeCapturePass.h"
#include "renderer/rhi/RhiHandles.h"
#include "renderer/rhi/RhiTypes.h"

class IDeferredGeometryProvider;
class ImGuiRhiRenderer;
class ResourceMgr;
class RhiCommandListPool;
class RhiDevice;
namespace renderer::core {
class GlobalBindlessSet;
}
struct RenderSettings;
struct GpuFrameStats;
struct RenderGraphFrameStats;
enum class WeatherType;

/// Owns the shared deferred environment used by the standalone model scene.
class ModelSceneDeferredRenderer {
public:
    ModelSceneDeferredRenderer();
    ~ModelSceneDeferredRenderer();

    ModelSceneDeferredRenderer(const ModelSceneDeferredRenderer&) = delete;
    ModelSceneDeferredRenderer& operator=(const ModelSceneDeferredRenderer&) = delete;

    /// Initializes deferred targets, atmosphere, shadows, and post-processing.
    [[nodiscard]] bool init(ResourceMgr& resourceMgr, RhiDevice& rhiDevice, RhiCommandListPool& commandListPool,
                            ImGuiRhiRenderer& imguiRenderer, IDeferredGeometryProvider& geometryProvider);
    void shutdown();

    /// Allocates render targets and a stable ImGui texture binding for the viewport.
    [[nodiscard]] bool ensureViewport(uint32_t width, uint32_t height);

    /// Renders one deferred scene frame and composites it into the viewport texture.
    [[nodiscard]] bool render(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& cameraPosition,
                              float nearPlane, float farPlane, float verticalFovDegrees,
                              const RenderFrameClock& frameClock);

    /// Publishes the model scene's complete reflection-probe source snapshot.
    /// @param renderer Scene callback that records one HDR cubemap face.
    /// @param sources Stable model-scene probe placement and capture revisions.
    /// @return True when the deferred capture pass accepted the configuration.
    [[nodiscard]] bool configureReflectionProbeCapture(IReflectionProbeCaptureRenderer& renderer,
                                                       std::vector<ReflectionProbeCaptureSource> sources);

    /// Changes the standalone environment time and invalidates temporal history.
    /// @param timeOfDaySeconds Time within the 1200-second world day.
    void setTimeOfDay(float timeOfDaySeconds);

    /// Returns the current environment time within the 1200-second world day.
    [[nodiscard]] float timeOfDay() const;

    /// Controls whether standalone scene time advances with real frame time.
    void setTimePaused(bool paused);
    [[nodiscard]] bool timePaused() const;

    /// Sets the multiplier applied to real frame time while scene time advances.
    void setTimeScale(float scale);
    [[nodiscard]] float timeScale() const;

    /// Changes the target weather using either an immediate or natural transition.
    void setWeather(WeatherType weather, bool instant);
    [[nodiscard]] WeatherType weather() const;
    [[nodiscard]] bool weatherTransitionInstant() const;

    /// Replaces the standalone renderer configuration and resets temporal history.
    /// @param settings Complete deferred renderer configuration for subsequent frames.
    void setSettings(const RenderSettings& settings);

    /// Returns the renderer configuration supported by a new standalone scene.
    [[nodiscard]] static RenderSettings defaultSettings();

    /// Verifies that settings do not require gameplay-only rendering resources.
    [[nodiscard]] static bool validateSettings(const RenderSettings& settings, std::string& error);

    /// Returns the active standalone renderer configuration.
    [[nodiscard]] const RenderSettings& settings() const;
    [[nodiscard]] bool isFsr1Supported() const;
    [[nodiscard]] bool isFsr31Supported() const;

    [[nodiscard]] uint64_t viewportTextureId() const;
    [[nodiscard]] uint32_t viewportWidth() const;
    [[nodiscard]] uint32_t viewportHeight() const;
    [[nodiscard]] RhiTextureHandle captureTextureHandle() const;
    [[nodiscard]] RhiTextureFormat captureTextureFormat() const;
    [[nodiscard]] RhiTextureHandle rtgiRawDiffuseTextureHandle() const;
    [[nodiscard]] RhiTextureHandle nrdDiffuseTextureHandle() const;
    [[nodiscard]] RhiTextureHandle rtgiLeakageNormalTextureHandle() const;
    [[nodiscard]] RhiTextureHandle rtgiLeakageViewZTextureHandle() const;
    [[nodiscard]] float nrdDiffuseToPreExposedScale() const;
    [[nodiscard]] const GpuFrameStats* gpuFrameStats() const;
    [[nodiscard]] RenderGraphFrameStats renderGraphFrameStats() const;
    [[nodiscard]] ReflectionProbeCaptureFrameStats reflectionProbeCaptureStats() const;
    /// Reports whether the model TLAS has no pending or retired generation.
    [[nodiscard]] bool isAccelerationStructureReady() const;
    /// Discards the just-rendered validation frame when scene resources were not ready.
    void discardValidationTemporalFrame();
    /// Returns the Vulkan Global Bindless Set used by imported model assets, or nullptr on OpenGL.
    [[nodiscard]] renderer::core::GlobalBindlessSet* globalBindlessSet();
    [[nodiscard]] const std::string& lastError() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

#endif // MECRAFT_MODEL_SCENE_DEFERRED_RENDERER_H
