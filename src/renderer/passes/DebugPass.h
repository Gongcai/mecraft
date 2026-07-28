#ifndef MECRAFT_DEBUG_PASS_H
#define MECRAFT_DEBUG_PASS_H

#include "RenderPass.h"
#include "../core/FrameContext.h"
#include "../core/RenderSettings.h"
#include "../rhi/RhiHandles.h"
#include "../rhi/RhiRenderGraph.h"

#include <array>
#include <cstddef>
#include <cstdint>

class DeferredRenderTargets;
class ResourceMgr;
class RhiDevice;
class RhiCommandList;

namespace shadow { class ShadowRenderer; }

/// Debug visualization pass: renders GBuffer/lighting/shadow/SSAO debug overlays.
/// Displays all intermediate render targets for visual inspection.
class DebugPass : public RenderPass {
public:
    static constexpr std::size_t kTextureCount = 25u;
    void init(ResourceMgr& resourceMgr);
    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "Debug"; }

    /// Inject external dependencies (non-owning pointers).
    void setShadowRenderer(shadow::ShadowRenderer* sr) { m_shadowRenderer = sr; }

    /// Graph handles for every texture that can be selected by a debug mode.
    struct GraphResources {
        std::array<RgTextureHandle, kTextureCount> textures;
        RgTextureHandle output;
    };

    /// Adds the debug visualization pass to the render graph.
    /// @param graph Graph receiving the debug pass.
    /// @param ctx Frame state retained until graph execution completes.
    /// @param settings Current debug and shadow settings.
    /// @param targets Persistent render targets used by the recording callback.
    /// @param resources Imported graph handles matching the shader bindings.
    /// @param dependency Pass that must complete before debug visualization.
    /// @return Debug pass handle, or an invalid handle for an invalid contract.
    [[nodiscard]] RgPassHandle addGraphPass(
        RenderGraph& graph,
        const FrameContext& ctx,
        const RenderSettings& settings,
        DeferredRenderTargets& targets,
        const GraphResources& resources,
        RgPassHandle dependency);

    /// Records the debug visualization into an existing graph command list.
    [[nodiscard]] bool recordGraphPass(
        const FrameContext& ctx,
        const RenderSettings& settings,
        DeferredRenderTargets& targets,
        int width,
        int height,
        RhiCommandList& commandList);

private:
    bool ensureRhiPipeline(RhiDevice& rhiDevice);
    bool ensureNoiseTextureView(RhiDevice& rhiDevice);
    bool ensureRhiBindGroup(RhiDevice& rhiDevice, int debugViewMode,
                            const std::array<RhiTextureViewHandle, kTextureCount>& views);
    void destroyRhiBindGroup();
    void destroyRhiResources();

    RhiDevice* m_rhiDevice = nullptr;
    RhiTextureHandle m_noiseTexture;
    RhiTextureHandle m_noiseViewTexture;
    RhiTextureViewHandle m_noiseTextureView;
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
    std::array<RhiTextureViewHandle, kTextureCount> m_boundViews = {};
    int m_boundDebugViewMode = -1;
    shadow::ShadowRenderer* m_shadowRenderer = nullptr;
};

#endif // MECRAFT_DEBUG_PASS_H
