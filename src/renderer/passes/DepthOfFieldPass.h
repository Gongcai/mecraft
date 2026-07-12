#ifndef MECRAFT_DEPTH_OF_FIELD_PASS_H
#define MECRAFT_DEPTH_OF_FIELD_PASS_H

#include "RenderPass.h"
#include "../core/FrameContext.h"
#include "../core/RenderSettings.h"
#include "../rhi/RhiHandles.h"

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

    void execute(const FrameContext& ctx, const RenderSettings& settings,
                 DeferredRenderTargets& targets);

private:
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
