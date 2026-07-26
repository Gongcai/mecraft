#ifndef MECRAFT_REFLECTION_PASS_H
#define MECRAFT_REFLECTION_PASS_H

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

/// Reflection pass: SSR probe, bilateral filter, and temporal reprojection.
class ReflectionPass : public RenderPass {
public:
    void init(ResourceMgr& resourceMgr);
    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "Reflection"; }

    /// Graph handles for reflection inputs, scratch storage, and history.
    struct GraphResources {
        RgTextureHandle sceneLighting;
        RgTextureHandle depth;
        RgTextureHandle normalAo;
        RgTextureHandle material;
        RgTextureHandle materialAux;
        RgTextureHandle skyCapture;
        RgTextureHandle voxelLight;
        RgTextureHandle reflection;
        RgTextureHandle scratch;
        RgTextureHandle historyPrevious;
        RgTextureHandle velocity;
    };

    /// Adds reflection probe, filter, and temporal stages to a graph.
    /// @param graph Graph receiving reflection pass declarations.
    /// @param ctx Frame state retained until immediate graph execution completes.
    /// @param settings Reflection and debug settings for this frame.
    /// @param targets Persistent render targets used by recording callbacks.
    /// @param resources Imported graph handles for all reflection resources.
    /// @param dependency Pass that must complete before reflection starts.
    /// @return Final reflection pass handle, or an invalid handle for an invalid contract.
    [[nodiscard]] RgPassHandle addGraphPasses(
        RenderGraph& graph,
        const FrameContext& ctx,
        const RenderSettings& settings,
        DeferredRenderTargets& targets,
        const GraphResources& resources,
        RgPassHandle dependency);

private:
    [[nodiscard]] bool recordReflection(RhiCommandList& commandList,
                                        const FrameContext& ctx,
                                        const RenderSettings& settings,
                                        DeferredRenderTargets& targets);
    [[nodiscard]] bool recordScratchCopy(RhiCommandList& commandList,
                                         const FrameContext& ctx,
                                         DeferredRenderTargets& targets);
    [[nodiscard]] bool recordFilter(RhiCommandList& commandList,
                                    const FrameContext& ctx,
                                    const ReflectionSettings& reflection,
                                    DeferredRenderTargets& targets);
    [[nodiscard]] bool recordTemporal(RhiCommandList& commandList,
                                      const FrameContext& ctx,
                                      const ReflectionSettings& reflection,
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
