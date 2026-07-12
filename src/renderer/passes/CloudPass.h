#ifndef MECRAFT_CLOUD_PASS_H
#define MECRAFT_CLOUD_PASS_H

#include "RenderPass.h"
#include "../core/FrameContext.h"
#include "../core/RenderSettings.h"
#include "../rhi/RhiHandles.h"

#include <array>
#include <cstdint>

class DeferredRenderTargets;
class ResourceMgr;
class RhiDevice;

/// Cloud pass: ray-marched volumetric clouds with temporal reprojection.
class CloudPass : public RenderPass {
public:
    void init(ResourceMgr& resourceMgr);
    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "Cloud"; }

    void execute(const FrameContext& ctx, const RenderSettings& settings,
                 DeferredRenderTargets& targets);
    void invalidateHistory();

private:
    [[nodiscard]] bool shouldRenderClouds(const FrameContext& ctx, const RenderSettings& settings);
    bool ensureRhiPipeline(RhiDevice& rhiDevice);
    bool ensureBindGroup(RhiDevice& rhiDevice,
                         const std::array<RhiTextureViewHandle, 4>& views);
    bool ensureNoiseTextureView(RhiDevice& rhiDevice);
    void destroyBindGroup();
    void destroyRhiResources();
    void destroyNoiseTextureView();

    RhiTextureHandle m_noiseTexture;
    RhiTextureViewHandle m_noiseTextureView;
    RhiDevice* m_noiseViewDevice = nullptr;

    RhiDevice* m_rhiDevice = nullptr;
    RhiBufferHandle m_uniformBuffer;
    RhiSamplerHandle m_nearestSampler;
    RhiSamplerHandle m_linearSampler;
    RhiSamplerHandle m_noiseSampler;
    RhiBindGroupLayoutHandle m_bindGroupLayout;
    RhiPipelineLayoutHandle m_pipelineLayout;
    RhiShaderHandle m_vertexShader;
    RhiShaderHandle m_fragmentShader;
    RhiPipelineHandle m_pipeline;
    RhiBindGroupHandle m_bindGroup;
    std::array<RhiTextureViewHandle, 4> m_boundViews = {};

    bool m_hasRenderedClouds = false;
    glm::vec3 m_lastCameraPos = glm::vec3(0.0f);
    float m_lastWeatherSignal = 0.0f;
};

#endif // MECRAFT_CLOUD_PASS_H
