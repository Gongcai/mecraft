#ifndef MECRAFT_TEMPORAL_RESOLVE_PASS_H
#define MECRAFT_TEMPORAL_RESOLVE_PASS_H

#include "RenderPass.h"
#include "../core/FrameContext.h"
#include "../core/RenderSettings.h"
#include "../rhi/RhiHandles.h"

class DeferredRenderTargets;
class ResourceMgr;
class RhiDevice;

/// TAA resolve pass: blends current frame with reprojected history.
class TemporalResolvePass : public RenderPass {
public:
    void init(ResourceMgr& resourceMgr);
    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "TemporalResolve"; }

    void execute(const FrameContext& ctx, const RenderSettings& settings,
                 DeferredRenderTargets& targets);

private:
    bool ensureRhiPipeline(RhiDevice& rhiDevice);
    bool ensureRhiBindGroup(RhiDevice& rhiDevice,
                            int historyPrevIndex,
                            RhiTextureViewHandle currentView,
                            RhiTextureViewHandle historyView,
                            RhiTextureViewHandle velocityView,
                            RhiTextureViewHandle depthView,
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
    RhiTextureViewHandle m_boundVelocityView[2];
    RhiTextureViewHandle m_boundDepthView[2];
    RhiTextureViewHandle m_boundMaterialAuxView[2];
};

#endif // MECRAFT_TEMPORAL_RESOLVE_PASS_H
