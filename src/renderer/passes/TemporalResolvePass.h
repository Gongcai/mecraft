#ifndef MECRAFT_TEMPORAL_RESOLVE_PASS_H
#define MECRAFT_TEMPORAL_RESOLVE_PASS_H

#include "RenderPass.h"
#include "../core/FrameContext.h"
#include "../core/RenderSettings.h"
#include "../rhi/RhiHandles.h"
#include "../rhi/RhiRenderGraph.h"

class DeferredRenderTargets;
class ResourceMgr;
class RhiDevice;

/// TAA resolve pass: blends current frame with reprojected history.
class TemporalResolvePass : public RenderPass {
public:
    void init(ResourceMgr& resourceMgr);
    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "TemporalResolve"; }

    /// Graph handles for the current color, temporal scratch, and reprojection inputs.
    struct GraphResources {
        RgTextureHandle sceneResolved;
        RgTextureHandle temporalCurrent;
        RgTextureHandle historyPrevious;
        RgTextureHandle historyDepthPrevious; // Visible surface depth from the previous frame.
        RgTextureHandle velocity;
        RgTextureHandle depth;
        RgTextureHandle transparentDepth;
        RgTextureHandle reactiveMask;
        RgTextureHandle transparencyMask;
        RgTextureHandle materialAux;
    };

    /// Adds the temporal copy and resolve stages to the render graph.
    /// @param graph Graph receiving the temporal stages.
    /// @param ctx Frame state retained until graph execution completes.
    /// @param settings Current temporal and weather settings.
    /// @param targets Persistent render targets used by recording callbacks.
    /// @param resources Imported graph handles for all temporal resources.
    /// @param dependency Pass that must complete before the copy starts.
    /// @return Final temporal resolve pass handle, or an invalid handle for an invalid contract.
    [[nodiscard]] RgPassHandle addGraphPasses(
        RenderGraph& graph,
        const FrameContext& ctx,
        const RenderSettings& settings,
        DeferredRenderTargets& targets,
        const GraphResources& resources,
        RgPassHandle dependency);

private:
    [[nodiscard]] bool recordCurrentCopy(RhiCommandList& commandList,
                                         DeferredRenderTargets& targets);
    [[nodiscard]] bool recordResolve(RhiCommandList& commandList,
                                     const FrameContext& ctx,
                                     const RenderSettings& settings,
                                     DeferredRenderTargets& targets);
    bool ensureRhiPipeline(RhiDevice& rhiDevice);
    bool ensureRhiBindGroup(RhiDevice& rhiDevice,
                            int historyPrevIndex,
                            RhiTextureViewHandle currentView,
                            RhiTextureViewHandle historyView,
                            RhiTextureViewHandle historyDepthView,
                            RhiTextureViewHandle velocityView,
                            RhiTextureViewHandle depthView,
                            RhiTextureViewHandle transparentDepthView,
                            RhiTextureViewHandle reactiveMaskView,
                            RhiTextureViewHandle transparencyMaskView,
                            RhiTextureViewHandle materialAuxView);
    void destroyRhiBindGroup();
    void destroyRhiResources();

    RhiDevice* m_rhiDevice = nullptr;
    RhiSamplerHandle m_sampler;
    RhiBindGroupLayoutHandle m_bindGroupLayout;
    RhiPipelineLayoutHandle m_pipelineLayout;
    RhiShaderHandle m_vertexShader;
    RhiShaderHandle m_fragmentShader;
    RhiPipelineHandle m_pipeline;
    RhiBindGroupHandle m_bindGroup[2];
    RhiTextureViewHandle m_boundCurrentView[2];
    RhiTextureViewHandle m_boundHistoryView[2];
    RhiTextureViewHandle m_boundHistoryDepthView[2];
    RhiTextureViewHandle m_boundVelocityView[2];
    RhiTextureViewHandle m_boundDepthView[2];
    RhiTextureViewHandle m_boundTransparentDepthView[2];
    RhiTextureViewHandle m_boundReactiveMaskView[2];
    RhiTextureViewHandle m_boundTransparencyMaskView[2];
    RhiTextureViewHandle m_boundMaterialAuxView[2];
};

#endif // MECRAFT_TEMPORAL_RESOLVE_PASS_H
