#ifndef MECRAFT_VELOCITY_PASS_H
#define MECRAFT_VELOCITY_PASS_H

#include "RenderPass.h"
#include "../core/FrameContext.h"
#include "../core/RenderSettings.h"
#include "../rhi/RhiHandles.h"

class DeferredRenderTargets;
class ResourceMgr;
class RhiDevice;

/// Screen-space velocity pass: reprojects depth to compute per-pixel velocity.
class VelocityPass : public RenderPass {
public:
    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "Velocity"; }

    void execute(const FrameContext& ctx, const RenderSettings& settings,
                 DeferredRenderTargets& targets);

private:
    bool ensureRhiPipeline(RhiDevice& rhiDevice);
    bool ensureRhiBindGroup(RhiDevice& rhiDevice,
                            RhiTextureViewHandle depthView,
                            RhiTextureViewHandle perObjectVelocityView);
    void destroyRhiBindGroup();
    void destroyRhiResources();

    RhiDevice* m_rhiDevice = nullptr;
    RhiSamplerHandle m_sampler;
    RhiBindGroupLayoutHandle m_bindGroupLayout;
    RhiPipelineLayoutHandle m_pipelineLayout;
    RhiShaderHandle m_vertexShader;
    RhiShaderHandle m_fragmentShader;
    RhiPipelineHandle m_pipeline;
    RhiBindGroupHandle m_bindGroup;
    RhiTextureViewHandle m_boundDepthView;
    RhiTextureViewHandle m_boundPerObjectVelocityView;
};

#endif // MECRAFT_VELOCITY_PASS_H
