#ifndef MECRAFT_VOLUMETRIC_PASS_H
#define MECRAFT_VOLUMETRIC_PASS_H

#include "RenderPass.h"
#include "../core/FrameContext.h"
#include "../core/RenderSettings.h"
#include "../rhi/RhiHandles.h"

#include <array>
#include <cstdint>

class DeferredRenderTargets;
class ResourceMgr;
class RhiDevice;
class Shader;

namespace shadow { class ShadowRenderer; }

/// Volumetric fog pass: ray-marched fog with temporal reprojection and scene composite.
class VolumetricPass : public RenderPass {
public:
    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "Volumetric"; }

    void setShadowRenderer(shadow::ShadowRenderer* sr) { m_shadowRenderer = sr; }

    /// Check if all required shaders are loaded.
    [[nodiscard]] bool hasShaders() const {
        return m_volumetricFogShader != nullptr;
    }

    /// Check if temporal shader is available.
    [[nodiscard]] bool hasTemporalShader() const { return m_resourceMgr != nullptr; }

    /// Execute volumetric fog march, temporal resolve, and composite.
    /// @param ctx Frame context
    /// @param settings Render settings
    /// @param targets Deferred render targets
    /// @param hasPreviousFrame Whether temporal history is available
    void execute(const FrameContext& ctx, const RenderSettings& settings,
                 DeferredRenderTargets& targets, bool hasPreviousFrame);
    void invalidateHistory();

private:
    [[nodiscard]] bool shouldRenderFog(const FrameContext& ctx, const RenderSettings& settings,
                                       bool hasPreviousFrame) const;
    void renderFog(const FrameContext& ctx, const RenderSettings& settings,
                   DeferredRenderTargets& targets);
    void renderTemporal(const FrameContext& ctx, const RenderSettings& settings,
                        DeferredRenderTargets& targets);
    void composite(const FrameContext& ctx, const RenderSettings& settings,
                   DeferredRenderTargets& targets, bool hasPreviousFrame);
    bool ensureCompositeRhiPipeline(RhiDevice& rhiDevice);
    bool ensureCompositeBindGroup(RhiDevice& rhiDevice,
                                  const std::array<RhiTextureViewHandle, 3>& views);
    void destroyCompositeBindGroup();
    void destroyCompositeRhiResources();
    bool ensureTemporalRhiPipeline(RhiDevice& rhiDevice);
    bool ensureTemporalBindGroup(RhiDevice& rhiDevice,
                                 const std::array<RhiTextureViewHandle, 5>& views);
    void destroyTemporalBindGroup();
    void destroyTemporalRhiResources();

    Shader* m_volumetricFogShader = nullptr;
    shadow::ShadowRenderer* m_shadowRenderer = nullptr;
    ResourceMgr* m_resourceMgr = nullptr;
    RhiTextureHandle m_noiseTexture;
    RhiDevice* m_compositeRhiDevice = nullptr;
    RhiSamplerHandle m_compositeNearestSampler;
    RhiSamplerHandle m_compositeLinearSampler;
    RhiBindGroupLayoutHandle m_compositeBindGroupLayout;
    RhiPipelineLayoutHandle m_compositePipelineLayout;
    RhiShaderHandle m_compositeVertexShader;
    RhiShaderHandle m_compositeFragmentShader;
    RhiPipelineHandle m_compositePipeline;
    RhiBindGroupHandle m_compositeBindGroup;
    std::array<RhiTextureViewHandle, 3> m_compositeBoundViews = {};

    RhiDevice* m_temporalRhiDevice = nullptr;
    RhiSamplerHandle m_temporalNearestSampler;
    RhiSamplerHandle m_temporalLinearSampler;
    RhiBindGroupLayoutHandle m_temporalBindGroupLayout;
    RhiPipelineLayoutHandle m_temporalPipelineLayout;
    RhiShaderHandle m_temporalVertexShader;
    RhiShaderHandle m_temporalFragmentShader;
    RhiPipelineHandle m_temporalPipeline;
    RhiBindGroupHandle m_temporalBindGroup;
    std::array<RhiTextureViewHandle, 5> m_temporalBoundViews = {};
    bool m_hasRenderedFog = false;
    glm::vec3 m_lastCameraPos = glm::vec3(0.0f);
    float m_lastWeatherSignal = 0.0f;
};

#endif // MECRAFT_VOLUMETRIC_PASS_H
