#ifndef MECRAFT_DEFERRED_LIGHTING_PASS_H
#define MECRAFT_DEFERRED_LIGHTING_PASS_H

#include "RenderPass.h"
#include "../core/FrameContext.h"
#include "../core/RenderSettings.h"
#include "../rhi/RhiHandles.h"

#include <array>
#include <cstdint>

class DeferredRenderTargets;
class ResourceMgr;
class RhiCommandList;
class RhiDevice;

namespace shadow { class ShadowRenderer; }

/// Deferred lighting pass: computes full-scene lighting from GBuffer, shadows, SSAO, and atmosphere.
class DeferredLightingPass : public RenderPass {
public:
    void init(ResourceMgr& resourceMgr);
    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "DeferredLighting"; }

    /// Inject the shadow renderer for CSM uniform binding.
    void setShadowRenderer(shadow::ShadowRenderer* sr) { m_shadowRenderer = sr; }

    /// Set the held block light value (from player inventory).
    void setHeldBlockLightValue(int value) { m_heldBlockLightValue = value; }

    /// Records deferred lighting into a graph-owned command list.
    /// @param commandList Recording command list supplied by the Render Graph.
    /// @param ctx Current frame camera, weather, atmosphere, and lighting state.
    /// @param settings Current shadow, SSAO, and post-process settings.
    /// @param targets Persistent GBuffer inputs and scene-lighting output.
    /// @return True when resources were prepared and lighting commands were recorded.
    [[nodiscard]] bool execute(RhiCommandList& commandList,
                               const FrameContext& ctx,
                               const RenderSettings& settings,
                               DeferredRenderTargets& targets);

private:
    bool ensureRhiPipeline(RhiDevice& rhiDevice);
    bool ensureExternalTextureViews(RhiDevice& rhiDevice);
    bool ensureTextureView(RhiDevice& rhiDevice, RhiTextureHandle texture,
                           RhiTextureFormat format, RhiTextureHandle& viewTexture,
                           RhiTextureViewHandle& textureView);
    bool ensureRhiBindGroup(RhiDevice& rhiDevice,
                            const std::array<RhiTextureViewHandle, 20>& views);
    void destroyRhiBindGroup();
    void destroyExternalTextureViews();
    void destroyRhiResources();

    RhiDevice* m_rhiDevice = nullptr;
    shadow::ShadowRenderer* m_shadowRenderer = nullptr;
    int m_heldBlockLightValue = 0;
    RhiTextureHandle m_lightmapDayTexture;
    RhiTextureHandle m_lightmapNightTexture;
    RhiTextureHandle m_noiseTexture;
    RhiTextureHandle m_rippleNormalTexture;
    RhiTextureHandle m_lightmapDayViewTexture;
    RhiTextureHandle m_lightmapNightViewTexture;
    RhiTextureHandle m_noiseViewTexture;
    RhiTextureHandle m_rippleNormalViewTexture;
    RhiTextureViewHandle m_lightmapDayTextureView;
    RhiTextureViewHandle m_lightmapNightTextureView;
    RhiTextureViewHandle m_noiseTextureView;
    RhiTextureViewHandle m_rippleNormalTextureView;
    RhiBufferHandle m_uniformBuffer;
    RhiSamplerHandle m_nearestClampSampler;
    RhiSamplerHandle m_linearClampSampler;
    RhiSamplerHandle m_linearRepeatSampler;
    RhiSamplerHandle m_nearestBorderSampler;
    RhiSamplerHandle m_compareBorderSampler;
    RhiBindGroupLayoutHandle m_bindGroupLayout;
    RhiPipelineLayoutHandle m_pipelineLayout;
    RhiShaderHandle m_vertexShader;
    RhiShaderHandle m_fragmentShader;
    RhiPipelineHandle m_pipeline;
    RhiBindGroupHandle m_bindGroup;
    std::array<RhiTextureViewHandle, 20> m_boundViews = {};
};

#endif // MECRAFT_DEFERRED_LIGHTING_PASS_H
