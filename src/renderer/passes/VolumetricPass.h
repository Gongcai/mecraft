#ifndef MECRAFT_VOLUMETRIC_PASS_H
#define MECRAFT_VOLUMETRIC_PASS_H

#include "RenderPass.h"
#include "../core/FrameContext.h"
#include "../core/RenderSettings.h"
#include "../rhi/RhiHandles.h"
#include "../rhi/RhiRenderGraph.h"

#include <array>
#include <cstdint>

class DeferredRenderTargets;
struct GameResources;
class RhiDevice;
class RhiCommandList;

namespace shadow {
class ShadowRenderer;
}

/// Volumetric fog pass: ray-marched fog with temporal reprojection and scene composite.
class VolumetricPass : public RenderPass {
public:
    void init(GameResources& resources);
    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "Volumetric"; }

    void setShadowRenderer(shadow::ShadowRenderer* sr) { m_shadowRenderer = sr; }

    /// Check if all required shaders are loaded.
    [[nodiscard]] bool hasShaders() const { return m_resources != nullptr; }

    /// Check if temporal shader is available.
    [[nodiscard]] bool hasTemporalShader() const { return m_resources != nullptr; }

    struct GraphResources {
        RgTextureHandle depth;
        RgTextureHandle skyCapture;
        RgTextureHandle noise;
        RgTextureHandle atmosphereLut;
        RgTextureHandle shadowDepthOpaque;
        RgTextureHandle shadowDepthAll;
        RgTextureHandle shadowColor0;
        RgTextureHandle shadowColor1;
        RgTextureHandle velocity;
        RgTextureHandle historyDepthPrevious;
        RgTextureHandle halfRes;
        RgTextureHandle historyPrevious;
        RgTextureHandle historyCurrent;
        RgTextureHandle sceneComposite;
        RgTextureHandle sceneResolved;
    };

    /// Adds fog generation and temporal reprojection before transparent water
    /// samples the current frame's half-resolution volumetric texture.
    /// @param graph Render graph receiving the preparation passes.
    /// @param ctx Current frame inputs used by fog generation.
    /// @param settings Current render settings.
    /// @param targets Deferred targets resolved during graph execution.
    /// @param hasPreviousFrame Whether temporal history is valid this frame.
    /// @param resources Graph handles required by volumetric preparation.
    /// @param dependency Pass that must complete before preparation starts.
    /// @return Final preparation pass, or an invalid handle for an invalid contract.
    [[nodiscard]] RgPassHandle addGraphPreparationPasses(RenderGraph& graph, const FrameContext& ctx,
                                                         const RenderSettings& settings, DeferredRenderTargets& targets,
                                                         bool hasPreviousFrame, const GraphResources& resources,
                                                         RgPassHandle dependency);

    /// Adds the final scene composite after transparent geometry has completed.
    /// @param graph Render graph receiving the composite pass.
    /// @param ctx Current frame inputs used by the composite shader.
    /// @param settings Current render settings.
    /// @param targets Deferred targets resolved during graph execution.
    /// @param hasPreviousFrame Whether temporal history is valid this frame.
    /// @param resources Graph handles required by the composite pass.
    /// @param dependency Pass that must complete before compositing starts.
    /// @return Composite pass handle, or an invalid handle for an invalid contract.
    [[nodiscard]] RgPassHandle addGraphCompositePass(RenderGraph& graph, const FrameContext& ctx,
                                                     const RenderSettings& settings, DeferredRenderTargets& targets,
                                                     bool hasPreviousFrame, const GraphResources& resources,
                                                     RgPassHandle dependency);
    void finishGraphExecution(bool succeeded);
    [[nodiscard]] bool graphFramePrepared() const { return m_graphFramePrepared; }
    [[nodiscard]] bool graphWritesHistory() const { return m_graphWritesHistory; }
    void invalidateHistory();

private:
    [[nodiscard]] bool shouldRenderFog(const FrameContext& ctx, const RenderSettings& settings,
                                       bool hasPreviousFrame) const;
    [[nodiscard]] bool recordFogPass(RhiCommandList& commandList, const FrameContext& ctx,
                                     const RenderSettings& settings, DeferredRenderTargets& targets);
    [[nodiscard]] bool recordTemporalPass(RhiCommandList& commandList, const FrameContext& ctx,
                                          const RenderSettings& settings, DeferredRenderTargets& targets);
    [[nodiscard]] bool recordCompositePass(RhiCommandList& commandList, const FrameContext& ctx,
                                           const RenderSettings& settings, DeferredRenderTargets& targets,
                                           bool hasPreviousFrame);
    bool ensureCompositeRhiPipeline(RhiDevice& rhiDevice);
    bool ensureCompositeBindGroup(RhiDevice& rhiDevice, const std::array<RhiTextureViewHandle, 3>& views);
    void destroyCompositeBindGroup();
    void destroyCompositeRhiResources();
    bool ensureTemporalRhiPipeline(RhiDevice& rhiDevice);
    bool ensureTemporalBindGroup(RhiDevice& rhiDevice, const std::array<RhiTextureViewHandle, 5>& views);
    void destroyTemporalBindGroup();
    void destroyTemporalRhiResources();
    bool ensureFogRhiPipeline(RhiDevice& rhiDevice);
    bool ensureFogBindGroup(RhiDevice& rhiDevice, const std::array<RhiTextureViewHandle, 10>& views);
    bool ensureFogNoiseTextureView(RhiDevice& rhiDevice);
    void destroyFogBindGroup();
    void destroyFogRhiResources();
    void destroyFogNoiseTextureView();

    shadow::ShadowRenderer* m_shadowRenderer = nullptr;
    GameResources* m_resources = nullptr;
    RhiTextureHandle m_noiseTexture;
    RhiTextureViewHandle m_fogNoiseTextureView;
    RhiDevice* m_fogNoiseViewDevice = nullptr;

    RhiDevice* m_fogRhiDevice = nullptr;
    RhiBufferHandle m_fogUniformBuffer;
    RhiSamplerHandle m_fogNearestClampSampler;
    RhiSamplerHandle m_fogLinearClampSampler;
    RhiSamplerHandle m_fogLinearRepeatSampler;
    RhiSamplerHandle m_fogNearestBorderSampler;
    RhiSamplerHandle m_fogCompareBorderSampler;
    RhiBindGroupLayoutHandle m_fogBindGroupLayout;
    RhiPipelineLayoutHandle m_fogPipelineLayout;
    RhiShaderHandle m_fogVertexShader;
    RhiShaderHandle m_fogFragmentShader;
    RhiPipelineHandle m_fogPipeline;
    RhiBindGroupHandle m_fogBindGroup;
    std::array<RhiTextureViewHandle, 10> m_fogBoundViews = {};

    RhiDevice* m_compositeRhiDevice = nullptr;
    RhiSamplerHandle m_compositeNearestSampler;
    RhiSamplerHandle m_compositeLinearSampler;
    RhiBindGroupLayoutHandle m_compositeBindGroupLayout;
    RhiPipelineLayoutHandle m_compositePipelineLayout;
    RhiShaderHandle m_compositeVertexShader;
    RhiShaderHandle m_compositeFragmentShader;
    RhiPipelineHandle m_compositePipeline;
    RhiBindGroupHandle m_compositeBindGroup;
    std::array<RhiTextureViewHandle, 3> m_compositeBoundViews = {};

    RhiDevice* m_temporalRhiDevice = nullptr;
    RhiSamplerHandle m_temporalNearestSampler;
    RhiSamplerHandle m_temporalLinearSampler;
    RhiBindGroupLayoutHandle m_temporalBindGroupLayout;
    RhiPipelineLayoutHandle m_temporalPipelineLayout;
    RhiShaderHandle m_temporalVertexShader;
    RhiShaderHandle m_temporalFragmentShader;
    RhiPipelineHandle m_temporalPipeline;
    RhiBindGroupHandle m_temporalBindGroup;
    std::array<RhiTextureViewHandle, 5> m_temporalBoundViews = {};
    bool m_hasRenderedFog = false;
    glm::vec3 m_lastCameraPos = glm::vec3(0.0f);
    float m_lastWeatherSignal = 0.0f;
    bool m_graphFramePrepared = false;
    bool m_graphWritesHistory = false;
    bool m_pendingRenderedFog = false;
    glm::vec3 m_pendingCameraPos = glm::vec3(0.0f);
    float m_pendingWeatherSignal = 0.0f;
};

#endif // MECRAFT_VOLUMETRIC_PASS_H
