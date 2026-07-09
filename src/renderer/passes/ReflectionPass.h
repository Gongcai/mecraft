#ifndef MECRAFT_REFLECTION_PASS_H
#define MECRAFT_REFLECTION_PASS_H

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

/// Reflection pass: SSR probe, bilateral filter, and temporal reprojection.
class ReflectionPass : public RenderPass {
public:
    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "Reflection"; }

    void execute(const FrameContext& ctx, const RenderSettings& settings,
                 DeferredRenderTargets& targets);

private:
    void renderReflection(const FrameContext& ctx, const RenderSettings& settings,
                          DeferredRenderTargets& targets);
    void renderFilter(const FrameContext& ctx, const ReflectionSettings& reflection,
                      DeferredRenderTargets& targets);
    void renderTemporal(const FrameContext& ctx, const ReflectionSettings& reflection,
                        DeferredRenderTargets& targets);
    bool ensureTemporalRhiPipeline(RhiDevice& rhiDevice);
    bool ensureTemporalBindGroup(RhiDevice& rhiDevice,
                                 const std::array<RhiTextureViewHandle, 7>& views);
    void destroyTemporalBindGroup();
    void destroyTemporalRhiResources();

    Shader* m_reflectionShader = nullptr;
    Shader* m_reflectionFilterShader = nullptr;

    RhiTextureHandle m_noiseTexture;
    RhiTextureHandle m_rippleNormalTexture;
    ResourceMgr* m_resourceMgr = nullptr;

    RhiDevice* m_rhiDevice = nullptr;
    RhiSamplerHandle m_temporalNearestSampler;
    RhiSamplerHandle m_temporalLinearSampler;
    RhiBindGroupLayoutHandle m_temporalBindGroupLayout;
    RhiPipelineLayoutHandle m_temporalPipelineLayout;
    RhiShaderHandle m_temporalVertexShader;
    RhiShaderHandle m_temporalFragmentShader;
    RhiPipelineHandle m_temporalPipeline;
    RhiBindGroupHandle m_temporalBindGroup;
    std::array<RhiTextureViewHandle, 7> m_temporalBoundViews = {};
};

#endif // MECRAFT_REFLECTION_PASS_H
