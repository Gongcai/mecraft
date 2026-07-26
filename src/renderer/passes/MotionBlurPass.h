#ifndef MECRAFT_MOTION_BLUR_PASS_H
#define MECRAFT_MOTION_BLUR_PASS_H

#include "RenderPass.h"
#include "../core/FrameContext.h"
#include "../core/RenderSettings.h"
#include "../rhi/RhiHandles.h"
#include "../rhi/RhiRenderGraph.h"

class DeferredRenderTargets;
class ResourceMgr;
class RhiDevice;

/// Motion blur pass: velocity-based per-pixel blur.
class MotionBlurPass : public RenderPass {
public:
    void init(ResourceMgr& resourceMgr);
    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "MotionBlur"; }

    /// Graph handles for the scene ping-pong pair and motion inputs.
    struct GraphResources {
        RgTextureHandle sceneResolved;
        RgTextureHandle temporalCurrent;
        RgTextureHandle velocity;
        RgTextureHandle depth;
    };

    /// Adds the motion-blur resolve stage to the graph. The pass reads the
    /// current scene color chain buffer and renders into the other one.
    /// @param graph Graph receiving the motion-blur stages.
    /// @param ctx Frame state retained until graph execution completes.
    /// @param settings Current post-process settings.
    /// @param targets Persistent render targets used by recording callbacks.
    /// @param resources Imported graph handles for all motion resources.
    /// @param dependency Pass that must complete before the copy starts.
    /// @return Final motion-blur pass handle, or an invalid handle for an invalid contract.
    [[nodiscard]] RgPassHandle addGraphPasses(
        RenderGraph& graph,
        const FrameContext& ctx,
        const RenderSettings& settings,
        DeferredRenderTargets& targets,
        const GraphResources& resources,
        RgPassHandle dependency);

private:
    [[nodiscard]] bool recordBlur(RhiCommandList& commandList,
                                  const FrameContext& ctx,
                                  const RenderSettings& settings,
                                  DeferredRenderTargets& targets,
                                  int inputIndex);
    bool ensureRhiPipeline(RhiDevice& rhiDevice);
    bool ensureRhiBindGroup(RhiDevice& rhiDevice,
                            int historyIndex,
                            RhiTextureViewHandle sceneView,
                            RhiTextureViewHandle velocityView,
                            RhiTextureViewHandle depthView);
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
    RhiTextureViewHandle m_boundSceneView[2];
    RhiTextureViewHandle m_boundVelocityView[2];
    RhiTextureViewHandle m_boundDepthView[2];
};

#endif // MECRAFT_MOTION_BLUR_PASS_H
