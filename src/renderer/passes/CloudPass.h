#ifndef MECRAFT_CLOUD_PASS_H
#define MECRAFT_CLOUD_PASS_H

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

/// Cloud pass: ray-marched volumetric clouds with temporal reprojection.
class CloudPass : public RenderPass {
public:
    void init(ResourceMgr& resourceMgr);
    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "Cloud"; }

    /// Graph handles for the cloud inputs, temporal history, and output.
    struct GraphResources {
        RgTextureHandle depth;
        RgTextureHandle skyCapture;
        RgTextureHandle noise;
        RgTextureHandle historyPrevious;
        RgTextureHandle cloud;
    };

    /// Adds either a cloud render pass or a history reuse copy to the graph.
    /// @param graph Graph receiving the selected cloud update pass.
    /// @param ctx Frame state retained until immediate graph execution completes.
    /// @param settings Cloud update cadence and rendering settings for this frame.
    /// @param targets Persistent render targets used by recording callbacks.
    /// @param resources Imported graph handles for all cloud resources.
    /// @param dependency Pass that must complete before the cloud update starts.
    /// @return The cloud update pass handle, or an invalid handle for an invalid contract.
    [[nodiscard]] RgPassHandle addGraphPass(
        RenderGraph& graph,
        const FrameContext& ctx,
        const RenderSettings& settings,
        DeferredRenderTargets& targets,
        const GraphResources& resources,
        RgPassHandle dependency);

    /// Commits pending temporal state only after the complete graph is submitted.
    /// @param succeeded True when graph recording and submission completed successfully.
    void finishGraphExecution(bool succeeded);
    void invalidateHistory();

private:
    [[nodiscard]] bool shouldRenderClouds(const FrameContext& ctx,
                                          const RenderSettings& settings) const;
    [[nodiscard]] bool recordHistoryCopy(RhiCommandList& commandList,
                                         const FrameContext& ctx,
                                         DeferredRenderTargets& targets);
    [[nodiscard]] bool recordCloud(RhiCommandList& commandList,
                                   const FrameContext& ctx,
                                   DeferredRenderTargets& targets);
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
    bool m_graphFramePrepared = false;
    bool m_pendingCloudRender = false;
    glm::vec3 m_pendingCameraPos = glm::vec3(0.0f);
    float m_pendingWeatherSignal = 0.0f;
};

#endif // MECRAFT_CLOUD_PASS_H
