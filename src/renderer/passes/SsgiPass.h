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
class Shader;

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
                           DeferredRenderTargets& targets, uint32_t inputTexture,
                           uint32_t momentsTexture);
    void renderSsgiTemporal(const FrameContext& ctx, const SsgiSettings& ssgi,
                            DeferredRenderTargets& targets);
    bool ensureBaseRhiPipeline(RhiDevice& rhiDevice);
    bool ensureBaseBindGroup(RhiDevice& rhiDevice,
                             const std::array<RhiTextureViewHandle, 6>& views);
    bool ensureNoiseTextureView(RhiDevice& rhiDevice);
    void destroyBaseBindGroup();
    void destroyBaseRhiResources();
    void destroyNoiseTextureView();

    Shader* m_ssgiUpsampleShader = nullptr;
    Shader* m_ssgiDenoiseShader = nullptr;
    Shader* m_ssgiTemporalShader = nullptr;
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
};

#endif // MECRAFT_SSGI_PASS_H
