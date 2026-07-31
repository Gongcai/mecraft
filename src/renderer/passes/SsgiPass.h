#ifndef MECRAFT_SSGI_PASS_H
#define MECRAFT_SSGI_PASS_H

#include "RenderPass.h"
#include "../core/FrameContext.h"
#include "../core/RenderSettings.h"
#include "../rhi/RhiHandles.h"
#include "../rhi/RhiRenderGraph.h"

#include <array>
#include <cstdint>

class DeferredRenderTargets;
class ResourceMgr;
class RhiCommandList;
class RhiDevice;

/// Screen-space global illumination pass: half-res gather, depth-aware upsample, denoise, temporal accumulation.
class SsgiPass : public RenderPass {
public:
    void init(ResourceMgr& resourceMgr);
    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "SSGI"; }

    /// Graph handles for SSGI inputs, intermediates, and temporal history.
    struct GraphResources {
        RgTextureHandle sceneLighting;
        RgTextureHandle albedo;
        RgTextureHandle normalAo;
        RgTextureHandle materialAux;
        RgTextureHandle depth;
        RgTextureHandle noise;
        RgTextureHandle halfRes;
        RgTextureHandle output;
        std::array<RgTextureHandle, 2> denoise;
        RgTextureHandle velocity;
        RgTextureHandle historyDepthPrevious;
        RgTextureHandle historyPrevious;
        RgTextureHandle momentsHistoryPrevious;
        RgTextureHandle temporal;
        RgTextureHandle temporalMoments;
        RgTextureHandle historyCurrent;
        RgTextureHandle momentsHistoryCurrent;
    };

    /// Adds enabled SSGI stages and their exact texture dependencies to a graph.
    /// @param graph Graph receiving the SSGI pass declarations.
    /// @param ctx Frame state retained until immediate graph execution completes.
    /// @param settings Current SSGI and temporal projection settings.
    /// @param targets Persistent render targets used by recording callbacks.
    /// @param resources Imported graph handles for all SSGI resources.
    /// @param dependency Pass that must complete before SSGI starts.
    /// @return Final SSGI pass handle, or an invalid handle for an invalid contract.
    [[nodiscard]] RgPassHandle addGraphPasses(RenderGraph& graph, const FrameContext& ctx,
                                              const RenderSettings& settings, DeferredRenderTargets& targets,
                                              const GraphResources& resources, RgPassHandle dependency);

private:
    [[nodiscard]] bool recordSsgiBase(RhiCommandList& commandList, const FrameContext& ctx,
                                      const RenderSettings& settings, DeferredRenderTargets& targets);
    [[nodiscard]] bool recordSsgiUpsample(RhiCommandList& commandList, const FrameContext& ctx,
                                          DeferredRenderTargets& targets);
    [[nodiscard]] bool recordSsgiDenoiseIteration(RhiCommandList& commandList, const FrameContext& ctx,
                                                  const SsgiSettings& ssgi, DeferredRenderTargets& targets,
                                                  bool momentsEnabled, int iteration);
    [[nodiscard]] bool recordSsgiTemporal(RhiCommandList& commandList, const FrameContext& ctx,
                                          const SsgiSettings& ssgi, DeferredRenderTargets& targets);
    [[nodiscard]] bool recordSsgiHistoryCopy(RhiCommandList& commandList, const FrameContext& ctx,
                                             DeferredRenderTargets& targets);
    [[nodiscard]] bool recordSsgiOutputCopy(RhiCommandList& commandList, const FrameContext& ctx,
                                            DeferredRenderTargets& targets, RhiTextureHandle source);
    bool ensureBaseRhiPipeline(RhiDevice& rhiDevice);
    bool ensureBaseBindGroup(RhiDevice& rhiDevice, const std::array<RhiTextureViewHandle, 6>& views);
    bool ensureNoiseTextureView(RhiDevice& rhiDevice);
    void destroyBaseBindGroup();
    void destroyBaseRhiResources();
    void destroyNoiseTextureView();
    bool ensureUpsampleRhiPipeline(RhiDevice& rhiDevice);
    bool ensureUpsampleBindGroup(RhiDevice& rhiDevice, const std::array<RhiTextureViewHandle, 2>& views);
    void destroyUpsampleBindGroup();
    void destroyUpsampleRhiResources();
    bool ensureTemporalRhiPipeline(RhiDevice& rhiDevice);
    bool ensureTemporalBindGroup(RhiDevice& rhiDevice, const std::array<RhiTextureViewHandle, 7>& views);
    void destroyTemporalBindGroup();
    void destroyTemporalRhiResources();
    bool ensureDenoiseRhiPipelines(RhiDevice& rhiDevice);
    bool ensureDenoiseBindGroup(RhiDevice& rhiDevice, bool momentsEnabled, uint32_t cacheIndex,
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
