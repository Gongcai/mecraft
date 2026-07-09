#ifndef MECRAFT_SSAO_PASS_H
#define MECRAFT_SSAO_PASS_H

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

/// SSAO render pass: half-res raw, bilateral filter, depth-aware upsample, temporal reprojection.
/// Reads from DeferredRenderTargets (GBuffer depth, normals, velocity) and writes SSAO results.
class SsaoPass : public RenderPass {
public:
    SsaoPass() = default;
    ~SsaoPass() override = default;

    // RenderPass interface
    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "SSAO"; }

    /// Execute all enabled SSAO sub-passes.
    /// @param ctx Frame context (camera, frame index, dimensions)
    /// @param settings Render settings (ssao sub-settings)
    /// @param targets Deferred render targets (GBuffer, SSAO textures, fullscreen VAO)
    void execute(const FrameContext& ctx, const RenderSettings& settings,
                 DeferredRenderTargets& targets);

private:
    void renderSsaoBase(const FrameContext& ctx, const SsaoSettings& ssao,
                        DeferredRenderTargets& targets);
    void renderSsaoFilter(const FrameContext& ctx, DeferredRenderTargets& targets);
    void renderSsaoUpsample(const FrameContext& ctx, const SsaoSettings& ssao,
                            DeferredRenderTargets& targets);
    void renderSsaoTemporal(const FrameContext& ctx, const SsaoSettings& ssao,
                            DeferredRenderTargets& targets);
    bool ensureFilterRhiPipeline(RhiDevice& rhiDevice);
    bool ensureFilterBindGroup(RhiDevice& rhiDevice,
                               const std::array<RhiTextureViewHandle, 3>& views);
    void destroyFilterBindGroup();
    void destroyFilterRhiResources();
    bool ensureTemporalRhiPipeline(RhiDevice& rhiDevice);
    bool ensureTemporalBindGroup(RhiDevice& rhiDevice,
                                 const std::array<RhiTextureViewHandle, 4>& views);
    void destroyTemporalBindGroup();
    void destroyTemporalRhiResources();

    // Shaders (non-owning, loaded by ResourceMgr)
    Shader* m_ssaoShader = nullptr;
    Shader* m_ssaoUpsampleShader = nullptr;

    RhiTextureHandle m_noiseTexture;
    RhiDevice* m_filterRhiDevice = nullptr;
    RhiSamplerHandle m_filterSampler;
    RhiBindGroupLayoutHandle m_filterBindGroupLayout;
    RhiPipelineLayoutHandle m_filterPipelineLayout;
    RhiShaderHandle m_filterVertexShader;
    RhiShaderHandle m_filterFragmentShader;
    RhiPipelineHandle m_filterPipeline;
    RhiBindGroupHandle m_filterBindGroup;
    std::array<RhiTextureViewHandle, 3> m_filterBoundViews = {};

    RhiDevice* m_temporalRhiDevice = nullptr;
    RhiSamplerHandle m_temporalNearestSampler;
    RhiSamplerHandle m_temporalLinearSampler;
    RhiBindGroupLayoutHandle m_temporalBindGroupLayout;
    RhiPipelineLayoutHandle m_temporalPipelineLayout;
    RhiShaderHandle m_temporalVertexShader;
    RhiShaderHandle m_temporalFragmentShader;
    RhiPipelineHandle m_temporalPipeline;
    RhiBindGroupHandle m_temporalBindGroup;
    std::array<RhiTextureViewHandle, 4> m_temporalBoundViews = {};
};

#endif // MECRAFT_SSAO_PASS_H
