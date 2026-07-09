#ifndef MECRAFT_MOTION_BLUR_PASS_H
#define MECRAFT_MOTION_BLUR_PASS_H

#include "RenderPass.h"
#include "../core/FrameContext.h"
#include "../core/RenderSettings.h"
#include "../rhi/RhiHandles.h"

class DeferredRenderTargets;
class ResourceMgr;
class RhiDevice;

/// Motion blur pass: velocity-based per-pixel blur.
class MotionBlurPass : public RenderPass {
public:
    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "MotionBlur"; }

    void execute(const FrameContext& ctx, const RenderSettings& settings,
                 DeferredRenderTargets& targets);

private:
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
