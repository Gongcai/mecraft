#ifndef MECRAFT_DEBUG_PASS_H
#define MECRAFT_DEBUG_PASS_H

#include "RenderPass.h"
#include "../core/FrameContext.h"
#include "../core/RenderSettings.h"
#include "../rhi/RhiHandles.h"

#include <array>
#include <cstdint>

class DeferredRenderTargets;
class ResourceMgr;
class RhiDevice;

namespace shadow { class ShadowRenderer; }

/// Debug visualization pass: renders GBuffer/lighting/shadow/SSAO debug overlays.
/// Displays all intermediate render targets for visual inspection.
class DebugPass : public RenderPass {
public:
    void init(ResourceMgr& resourceMgr);
    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "Debug"; }

    /// Inject external dependencies (non-owning pointers).
    void setShadowRenderer(shadow::ShadowRenderer* sr) { m_shadowRenderer = sr; }

    /// Execute the debug overlay pass.
    /// @param ctx Frame context (camera, sky, timing)
    /// @param settings Render settings (debug view mode, shadow parameters)
    /// @param targets Deferred render targets (all intermediate textures)
    /// @param width Destination viewport width
    /// @param height Destination viewport height
    void execute(const FrameContext& ctx, const RenderSettings& settings,
                 DeferredRenderTargets& targets, int width, int height);

private:
    bool ensureRhiPipeline(RhiDevice& rhiDevice);
    bool ensureNoiseTextureView(RhiDevice& rhiDevice);
    bool ensureRhiBindGroup(RhiDevice& rhiDevice, int debugViewMode,
                            const std::array<RhiTextureViewHandle, 21>& views);
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
    std::array<RhiTextureViewHandle, 21> m_boundViews = {};
    int m_boundDebugViewMode = -1;
    shadow::ShadowRenderer* m_shadowRenderer = nullptr;
};

#endif // MECRAFT_DEBUG_PASS_H
