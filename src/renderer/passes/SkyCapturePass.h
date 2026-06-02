#ifndef MECRAFT_SKY_CAPTURE_PASS_H
#define MECRAFT_SKY_CAPTURE_PASS_H

#include "RenderPass.h"
#include <glm/glm.hpp>

class DeferredRenderTargets;
class GameplaySkyRenderer;
class ResourceMgr;
class DayNightSystem;
class WeatherSystem;

/// Sky capture pass: renders equirectangular sky radiance and cloud data for IBL and lighting.
class SkyCapturePass : public RenderPass {
public:
    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "SkyCapture"; }

    /// Execute sky capture rendering.
    /// @param dayNightSystem Day/night system for sky colors and time
    /// @param weatherSystem Weather system for wetness and storm state
    /// @param targets Deferred render targets (sky capture FBO, atmosphere LUT)
    /// @param skyRenderer Gameplay sky renderer (owns shaders and rendering)
    /// @param resourceMgr Resource manager for noise texture
    /// @param cameraY Camera altitude for atmosphere scattering
    /// @param shaderTime Current shader time
    /// @param cameraPos Camera position for cloud rendering
    /// @param cloudTimeScale Cloud time scale from settings
    void execute(const DayNightSystem& dayNightSystem, const WeatherSystem& weatherSystem,
                 DeferredRenderTargets& targets,
                 GameplaySkyRenderer& skyRenderer, ResourceMgr* resourceMgr,
                 float cameraY, float shaderTime, const glm::vec3& cameraPos,
                 float cloudTimeScale);
};

#endif // MECRAFT_SKY_CAPTURE_PASS_H
