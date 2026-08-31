#ifndef MECRAFT_SSAO_PASS_H
#define MECRAFT_SSAO_PASS_H

#include "RenderPass.h"
#include "../core/FrameContext.h"
#include "../core/RenderSettings.h"
#include "../rhi/RhiHandles.h"
#include "../rhi/RhiRenderGraph.h"

#include <array>
#include <cstdint>

class DeferredRenderTargets;
struct GameResources;
class RhiDevice;

/// SSAO render pass: half-res raw, bilateral filter, depth-aware upsample, temporal reprojection.
/// Reads from DeferredRenderTargets (GBuffer depth, normals, velocity) and writes SSAO results.
class SsaoPass : public RenderPass {
public:
    SsaoPass() = default;
    ~SsaoPass() override = default;

    void init(GameResources& resources);
    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "SSAO"; }

    /// Graph handles for GBuffer inputs, SSAO intermediates, and history textures.
    struct GraphResources {
        RgTextureHandle depth;
        RgTextureHandle normalAo;
        RgTextureHandle velocity;
        RgTextureHandle noise;
        RgTextureHandle halfRes;
        RgTextureHandle halfResFiltered;
        RgTextureHandle filtered;
        RgTextureHandle temporal;
        RgTextureHandle historyCurrent;
        RgTextureHandle historyPrevious;
    };

    /// Adds enabled SSAO stages and their exact texture dependencies to a graph.
    /// @param graph Graph receiving the SSAO pass declarations.
    /// @param ctx Frame state retained until immediate graph execution completes.
    /// @param ssao SSAO quality, filter, and temporal settings.
    /// @param targets Persistent render targets used by the recording callbacks.
    /// @param resources Imported graph handles for all SSAO resources.
    /// @param dependency Pass that must complete before SSAO starts.
    /// @return Final SSAO pass handle, or an invalid handle for an invalid contract.
    /// When useAsyncCompute is true, every stage is declared on the compute
    /// queue and writes its target through storage images so the chain
    /// overlaps the graphics work scheduled after it.
    [[nodiscard]] RgPassHandle addGraphPasses(RenderGraph& graph, const FrameContext& ctx, const SsaoSettings& ssao,
                                              DeferredRenderTargets& targets, const GraphResources& resources,
                                              RgPassHandle dependency, bool useAsyncCompute = false);

private:
    /// One async-compute mirror of a fragment stage: same bindings plus a
    /// storage image at the next binding slot, COMPUTE visibility throughout.
    struct ComputeStage {
        RhiDevice* device = nullptr;
        RhiShaderHandle shader;
        RhiBindGroupLayoutHandle bindGroupLayout;
        RhiPipelineLayoutHandle pipelineLayout;
        RhiPipelineHandle pipeline;
        RhiBindGroupHandle bindGroup;
        std::array<RhiTextureViewHandle, 5> boundViews = {};
    };
    bool ensureComputeStage(RhiDevice& rhiDevice, ComputeStage& stage, const char* shaderPath, const char* debugName,
                            uint32_t sampledCount, uint32_t pushConstantBytes);
    bool ensureComputeStageBindGroup(RhiDevice& rhiDevice, ComputeStage& stage, const RhiTextureViewHandle* views,
                                     const RhiSamplerHandle* samplers, uint32_t sampledCount,
                                     RhiTextureViewHandle storageView);
    void destroyComputeStage(ComputeStage& stage);
    [[nodiscard]] bool recordSsaoBaseCompute(RhiCommandList& commandList, const FrameContext& ctx,
                                             const SsaoSettings& ssao, DeferredRenderTargets& targets);
    [[nodiscard]] bool recordSsaoFilterCompute(RhiCommandList& commandList, const FrameContext& ctx,
                                               DeferredRenderTargets& targets);
    [[nodiscard]] bool recordSsaoUpsampleCompute(RhiCommandList& commandList, const FrameContext& ctx,
                                                 const SsaoSettings& ssao, DeferredRenderTargets& targets);
    [[nodiscard]] bool recordSsaoTemporalCompute(RhiCommandList& commandList, const FrameContext& ctx,
                                                 const SsaoSettings& ssao, DeferredRenderTargets& targets);
    [[nodiscard]] bool recordSsaoHistoryCopyCompute(RhiCommandList& commandList, const FrameContext& ctx,
                                                    DeferredRenderTargets& targets);
    ComputeStage m_computeBase;
    ComputeStage m_computeFilter;
    ComputeStage m_computeUpsample;
    ComputeStage m_computeTemporal;

    [[nodiscard]] bool recordSsaoBase(RhiCommandList& commandList, const FrameContext& ctx, const SsaoSettings& ssao,
                                      DeferredRenderTargets& targets);
    [[nodiscard]] bool recordSsaoFilter(RhiCommandList& commandList, const FrameContext& ctx,
                                        DeferredRenderTargets& targets);
    [[nodiscard]] bool recordSsaoUpsample(RhiCommandList& commandList, const FrameContext& ctx,
                                          const SsaoSettings& ssao, DeferredRenderTargets& targets);
    [[nodiscard]] bool recordSsaoTemporal(RhiCommandList& commandList, const FrameContext& ctx,
                                          const SsaoSettings& ssao, DeferredRenderTargets& targets);
    [[nodiscard]] bool recordSsaoHistoryCopy(RhiCommandList& commandList, const FrameContext& ctx,
                                             DeferredRenderTargets& targets);
    bool ensureBaseRhiPipeline(RhiDevice& rhiDevice);
    bool ensureBaseBindGroup(RhiDevice& rhiDevice, const std::array<RhiTextureViewHandle, 3>& views);
    bool ensureNoiseTextureView(RhiDevice& rhiDevice);
    void destroyBaseBindGroup();
    void destroyBaseRhiResources();
    void destroyNoiseTextureView();
    bool ensureFilterRhiPipeline(RhiDevice& rhiDevice);
    bool ensureFilterBindGroup(RhiDevice& rhiDevice, const std::array<RhiTextureViewHandle, 3>& views);
    void destroyFilterBindGroup();
    void destroyFilterRhiResources();
    bool ensureUpsampleRhiPipeline(RhiDevice& rhiDevice);
    bool ensureUpsampleBindGroup(RhiDevice& rhiDevice, const std::array<RhiTextureViewHandle, 2>& views);
    void destroyUpsampleBindGroup();
    void destroyUpsampleRhiResources();
    bool ensureTemporalRhiPipeline(RhiDevice& rhiDevice);
    bool ensureTemporalBindGroup(RhiDevice& rhiDevice, const std::array<RhiTextureViewHandle, 4>& views);
    void destroyTemporalBindGroup();
    void destroyTemporalRhiResources();

    RhiTextureHandle m_noiseTexture;
    RhiTextureViewHandle m_noiseTextureView;
    RhiDevice* m_noiseViewDevice = nullptr;

    RhiDevice* m_baseRhiDevice = nullptr;
    RhiSamplerHandle m_baseNearestSampler;
    RhiSamplerHandle m_baseNoiseSampler;
    RhiBindGroupLayoutHandle m_baseBindGroupLayout;
    RhiPipelineLayoutHandle m_basePipelineLayout;
    RhiShaderHandle m_baseVertexShader;
    RhiShaderHandle m_baseFragmentShader;
    RhiPipelineHandle m_basePipeline;
    RhiBindGroupHandle m_baseBindGroup;
    std::array<RhiTextureViewHandle, 3> m_baseBoundViews = {};

    RhiDevice* m_filterRhiDevice = nullptr;
    RhiSamplerHandle m_filterSampler;
    RhiBindGroupLayoutHandle m_filterBindGroupLayout;
    RhiPipelineLayoutHandle m_filterPipelineLayout;
    RhiShaderHandle m_filterVertexShader;
    RhiShaderHandle m_filterFragmentShader;
    RhiPipelineHandle m_filterPipeline;
    RhiBindGroupHandle m_filterBindGroup;
    std::array<RhiTextureViewHandle, 3> m_filterBoundViews = {};

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
    std::array<RhiTextureViewHandle, 4> m_temporalBoundViews = {};
};

#endif // MECRAFT_SSAO_PASS_H
