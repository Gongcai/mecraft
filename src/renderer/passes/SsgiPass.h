#ifndef MECRAFT_SSGI_PASS_H
#define MECRAFT_SSGI_PASS_H

#include "RenderPass.h"
#include "../core/FrameContext.h"
#include "../core/RenderSettings.h"
#include "../rhi/RhiHandles.h"

#include <array>
#include <cstdint>

class DeferredRenderTargets;
class ResourceMgr;
class RhiDevice;

/// Screen-space global illumination pass: half-res gather, depth-aware upsample, denoise, temporal accumulation.
class SsgiPass : public RenderPass {
public:
    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "SSGI"; }

    void execute(const FrameContext& ctx, const RenderSettings& settings,
                 DeferredRenderTargets& targets);

private:
    void renderSsgiBase(const FrameContext& ctx, const RenderSettings& settings,
                        DeferredRenderTargets& targets);
    void renderSsgiUpsample(const FrameContext& ctx, DeferredRenderTargets& targets);
    void renderSsgiDenoise(const FrameContext& ctx, const SsgiSettings& ssgi,
                           DeferredRenderTargets& targets, bool momentsEnabled);
    void renderSsgiTemporal(const FrameContext& ctx, const SsgiSettings& ssgi,
                            DeferredRenderTargets& targets);
    bool ensureBaseRhiPipeline(RhiDevice& rhiDevice);
    bool ensureBaseBindGroup(RhiDevice& rhiDevice,
                             const std::array<RhiTextureViewHandle, 6>& views);
    bool ensureNoiseTextureView(RhiDevice& rhiDevice);
    void destroyBaseBindGroup();
    void destroyBaseRhiResources();
    void destroyNoiseTextureView();
    bool ensureUpsampleRhiPipeline(RhiDevice& rhiDevice);
    bool ensureUpsampleBindGroup(RhiDevice& rhiDevice,
                                 const std::array<RhiTextureViewHandle, 2>& views);
    void destroyUpsampleBindGroup();
    void destroyUpsampleRhiResources();
    bool ensureTemporalRhiPipeline(RhiDevice& rhiDevice);
    bool ensureTemporalBindGroup(RhiDevice& rhiDevice,
                                 const std::array<RhiTextureViewHandle, 7>& views);
    void destroyTemporalBindGroup();
    void destroyTemporalRhiResources();
    bool ensureDenoiseRhiPipelines(RhiDevice& rhiDevice);
    bool ensureDenoiseBindGroup(RhiDevice& rhiDevice, bool momentsEnabled,
                                uint32_t cacheIndex,
                                const std::array<RhiTextureViewHandle, 4>& views);
    void destroyDenoiseBindGroups();
    void destroyDenoiseRhiResources();

    RhiTextureHandle m_noiseTexture;
    RhiTextureViewHandle m_noiseTextureView;
    RhiDevice* m_noiseViewDevice = nullptr;

    RhiDevice* m_baseRhiDevice = nullptr;
    RhiBufferHandle m_baseUniformBuffer;
    RhiSamplerHandle m_baseNearestSampler;
    RhiSamplerHandle m_baseLinearSampler;
    RhiSamplerHandle m_baseNoiseSampler;
    RhiBindGroupLayoutHandle m_baseBindGroupLayout;
    RhiPipelineLayoutHandle m_basePipelineLayout;
    RhiShaderHandle m_baseVertexShader;
    RhiShaderHandle m_baseFragmentShader;
    RhiPipelineHandle m_basePipeline;
    RhiBindGroupHandle m_baseBindGroup;
    std::array<RhiTextureViewHandle, 6> m_baseBoundViews = {};

    RhiDevice* m_upsampleRhiDevice = nullptr;
    RhiSamplerHandle m_upsampleNearestSampler;
    RhiSamplerHandle m_upsampleLinearSampler;
    RhiBindGroupLayoutHandle m_upsampleBindGroupLayout;
    RhiPipelineLayoutHandle m_upsamplePipelineLayout;
    RhiShaderHandle m_upsampleVertexShader;
    RhiShaderHandle m_upsampleFragmentShader;
    RhiPipelineHandle m_upsamplePipeline;
    RhiBindGroupHandle m_upsampleBindGroup;
    std::array<RhiTextureViewHandle, 2> m_upsampleBoundViews = {};

    RhiDevice* m_temporalRhiDevice = nullptr;
    RhiSamplerHandle m_temporalNearestSampler;
    RhiSamplerHandle m_temporalLinearSampler;
    RhiBindGroupLayoutHandle m_temporalBindGroupLayout;
    RhiPipelineLayoutHandle m_temporalPipelineLayout;
    RhiShaderHandle m_temporalVertexShader;
    RhiShaderHandle m_temporalFragmentShader;
    RhiPipelineHandle m_temporalPipeline;
    RhiBindGroupHandle m_temporalBindGroup;
    std::array<RhiTextureViewHandle, 7> m_temporalBoundViews = {};

    RhiDevice* m_denoiseRhiDevice = nullptr;
    RhiSamplerHandle m_denoiseNearestSampler;
    RhiSamplerHandle m_denoiseLinearSampler;
    RhiBindGroupLayoutHandle m_denoiseSpatialBindGroupLayout;
    RhiBindGroupLayoutHandle m_denoiseMomentsBindGroupLayout;
    RhiPipelineLayoutHandle m_denoiseSpatialPipelineLayout;
    RhiPipelineLayoutHandle m_denoiseMomentsPipelineLayout;
    RhiShaderHandle m_denoiseVertexShader;
    RhiShaderHandle m_denoiseSpatialFragmentShader;
    RhiShaderHandle m_denoiseMomentsFragmentShader;
    RhiPipelineHandle m_denoiseSpatialPipeline;
    RhiPipelineHandle m_denoiseMomentsPipeline;
    std::array<RhiBindGroupHandle, 3> m_denoiseSpatialBindGroups = {};
    std::array<RhiBindGroupHandle, 3> m_denoiseMomentsBindGroups = {};
    std::array<std::array<RhiTextureViewHandle, 4>, 3> m_denoiseSpatialBoundViews = {};
    std::array<std::array<RhiTextureViewHandle, 4>, 3> m_denoiseMomentsBoundViews = {};
};

#endif // MECRAFT_SSGI_PASS_H
