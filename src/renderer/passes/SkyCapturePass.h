#ifndef MECRAFT_SKY_CAPTURE_PASS_H
#define MECRAFT_SKY_CAPTURE_PASS_H

#include "RenderPass.h"
#include "../rhi/RhiHandles.h"
#include <glm/glm.hpp>

class DeferredRenderTargets;
class GameplaySkyRenderer;
struct GameResources;
class DayNightSystem;
class WeatherSystem;
class RhiDevice;
class RhiCommandList;

/// Sky capture pass: renders equirectangular sky radiance and cloud data for IBL and lighting.
class SkyCapturePass : public RenderPass {
public:
    void init(GameResources& resources);
    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "SkyCapture"; }

    /// Records sky capture rendering into a graph-owned command list.
    /// @param commandList Recording command list supplied by the Render Graph.
    /// @param dayNightSystem Day/night system for sky colors and time
    /// @param weatherSystem Weather system for wetness and storm state
    /// @param rhiDevice RHI device that owns the sky capture resources.
    /// @param targets Deferred render targets for sky capture and atmosphere LUT resources
    /// @param skyRenderer Gameplay sky renderer (owns shaders and rendering)
    /// @param resources Resource manager for noise texture
    /// @param cameraY Camera altitude for atmosphere scattering
    /// @param shaderTime Current shader time
    /// @param cameraPos Camera position for cloud rendering
    /// @param cloudTimeScale Cloud time scale from settings
    /// @return True when all resources were prepared and commands were recorded.
    [[nodiscard]] bool execute(RhiCommandList& commandList, const DayNightSystem& dayNightSystem,
                               const WeatherSystem& weatherSystem, RhiDevice& rhiDevice, DeferredRenderTargets& targets,
                               GameplaySkyRenderer& skyRenderer, GameResources& resources, float cameraY,
                               float shaderTime, const glm::vec3& cameraPos, float cloudTimeScale);

private:
    [[nodiscard]] bool ensureMetadataResources(RhiDevice& rhiDevice, RhiTextureViewHandle atmosphereLutView);
    void destroyMetadataResources();

    RhiDevice* m_rhiDevice = nullptr;
    RhiSamplerHandle m_metadataSampler;
    RhiShaderHandle m_metadataVertexShader;
    RhiShaderHandle m_rawFragmentShader;
    RhiShaderHandle m_metadataFragmentShader;
    RhiBindGroupLayoutHandle m_metadataBindGroupLayout;
    RhiPipelineLayoutHandle m_metadataPipelineLayout;
    RhiPipelineHandle m_metadataPipeline;
    RhiPipelineHandle m_rawPipeline;
    RhiBindGroupHandle m_metadataBindGroup;
    RhiTextureViewHandle m_boundAtmosphereLutView;
};

#endif // MECRAFT_SKY_CAPTURE_PASS_H
