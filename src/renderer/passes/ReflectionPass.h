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

/// Reflection pass: SSR probe, bilateral filter, and temporal reprojection.
class ReflectionPass : public RenderPass {
public:
    void init(ResourceMgr& resourceMgr);
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
    bool ensureBaseRhiPipeline(RhiDevice& rhiDevice);
    bool ensureBaseBindGroup(RhiDevice& rhiDevice,
                             const std::array<RhiTextureViewHandle, 7>& views);
    void destroyBaseBindGroup();
    void destroyBaseRhiResources();
    bool ensureFilterRhiPipeline(RhiDevice& rhiDevice);
    bool ensureFilterBindGroup(RhiDevice& rhiDevice,
                               const std::array<RhiTextureViewHandle, 5>& views);
    void destroyFilterBindGroup();
    void destroyFilterRhiResources();
    bool ensureTemporalRhiPipeline(RhiDevice& rhiDevice);
    bool ensureTemporalBindGroup(RhiDevice& rhiDevice,
                                 const std::array<RhiTextureViewHandle, 7>& views);
    void destroyTemporalBindGroup();
    void destroyTemporalRhiResources();

    RhiDevice* m_baseRhiDevice = nullptr;
    RhiBufferHandle m_baseUniformBuffer;
    RhiSamplerHandle m_baseNearestSampler;
    RhiSamplerHandle m_baseLinearSampler;
    RhiBindGroupLayoutHandle m_baseBindGroupLayout;
    RhiPipelineLayoutHandle m_basePipelineLayout;
    RhiShaderHandle m_baseVertexShader;
    RhiShaderHandle m_baseFragmentShader;
    RhiPipelineHandle m_basePipeline;
    RhiBindGroupHandle m_baseBindGroup;
    std::array<RhiTextureViewHandle, 7> m_baseBoundViews = {};

    RhiDevice* m_filterRhiDevice = nullptr;
    RhiSamplerHandle m_filterNearestSampler;
    RhiSamplerHandle m_filterLinearSampler;
    RhiBindGroupLayoutHandle m_filterBindGroupLayout;
    RhiPipelineLayoutHandle m_filterPipelineLayout;
    RhiShaderHandle m_filterVertexShader;
    RhiShaderHandle m_filterFragmentShader;
    RhiPipelineHandle m_filterPipeline;
    RhiBindGroupHandle m_filterBindGroup;
    std::array<RhiTextureViewHandle, 5> m_filterBoundViews = {};

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
