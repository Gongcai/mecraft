#ifndef MECRAFT_DEPTH_OF_FIELD_PASS_H
#define MECRAFT_DEPTH_OF_FIELD_PASS_H

#include "RenderPass.h"
#include "../core/FrameContext.h"
#include "../core/RenderSettings.h"
#include "../rhi/RhiHandles.h"
#include "../rhi/RhiRenderGraph.h"

#include <cstdint>

class DeferredRenderTargets;
class ResourceMgr;
class RhiDevice;

/// Depth of field pass: circle-of-confusion based blur.
class DepthOfFieldPass : public RenderPass {
public:
    void init(ResourceMgr& resourceMgr);
    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "DepthOfField"; }

    /// Graph handles for the scene scratch and depth input.
    struct GraphResources {
        RgTextureHandle sceneResolved;
        RgTextureHandle historyCurrent;
        RgTextureHandle depth;
    };

    /// Adds the depth-of-field scratch copy and resolve stages to the graph.
    /// @param graph Graph receiving the depth-of-field stages.
    /// @param ctx Frame state retained until graph execution completes.
    /// @param settings Current post-process settings.
    /// @param targets Persistent render targets used by recording callbacks.
    /// @param resources Imported graph handles for all depth-of-field resources.
    /// @param dependency Pass that must complete before the copy starts.
    /// @return Final depth-of-field pass handle, or an invalid handle for an invalid contract.
    [[nodiscard]] RgPassHandle addGraphPasses(
        RenderGraph& graph,
        const FrameContext& ctx,
        const RenderSettings& settings,
        DeferredRenderTargets& targets,
        const GraphResources& resources,
        RgPassHandle dependency);

private:
    [[nodiscard]] bool recordHistoryCopy(RhiCommandList& commandList,
                                         DeferredRenderTargets& targets);
    [[nodiscard]] bool recordDof(RhiCommandList& commandList,
                                 const FrameContext& ctx,
                                 const RenderSettings& settings,
                                 DeferredRenderTargets& targets);
    bool ensureRhiPipeline(RhiDevice& rhiDevice);
    bool ensureNoiseTextureView(RhiDevice& rhiDevice);
    bool ensureRhiBindGroup(RhiDevice& rhiDevice,
                            int historyIndex,
                            RhiTextureViewHandle sceneView,
                            RhiTextureViewHandle depthView,
                            RhiTextureViewHandle noiseView);
    void destroyRhiBindGroup();
    void destroyRhiResources();

    RhiDevice* m_rhiDevice = nullptr;
    RhiTextureHandle m_noiseTexture;
    RhiTextureHandle m_noiseViewTexture;
    RhiTextureViewHandle m_noiseTextureView;
    RhiSamplerHandle m_sampler;
    RhiBindGroupLayoutHandle m_bindGroupLayout;
    RhiPipelineLayoutHandle m_pipelineLayout;
    RhiShaderHandle m_vertexShader;
    RhiShaderHandle m_fragmentShader;
    RhiPipelineHandle m_pipeline;
    RhiBindGroupHandle m_bindGroup[2];
    RhiTextureViewHandle m_boundSceneView[2];
    RhiTextureViewHandle m_boundDepthView[2];
    RhiTextureViewHandle m_boundNoiseView[2];
};

#endif // MECRAFT_DEPTH_OF_FIELD_PASS_H
