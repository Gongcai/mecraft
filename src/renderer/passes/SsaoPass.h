#ifndef MECRAFT_SSAO_PASS_H
#define MECRAFT_SSAO_PASS_H

#include "RenderPass.h"
#include "../core/FrameContext.h"
#include "../core/RenderSettings.h"
#include "../rhi/RhiHandles.h"

#include <cstdint>

class DeferredRenderTargets;
class ResourceMgr;
class Shader;

/// SSAO render pass: half-res raw, bilateral filter, depth-aware upsample, temporal reprojection.
/// Reads from DeferredRenderTargets (GBuffer depth, normals, velocity) and writes SSAO results.
class SsaoPass : public RenderPass {
public:
    SsaoPass() = default;
    ~SsaoPass() override = default;

    // RenderPass interface
    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "SSAO"; }

    /// Execute all enabled SSAO sub-passes.
    /// @param ctx Frame context (camera, frame index, dimensions)
    /// @param settings Render settings (ssao sub-settings)
    /// @param targets Deferred render targets (GBuffer, SSAO textures, fullscreen VAO)
    void execute(const FrameContext& ctx, const RenderSettings& settings,
                 DeferredRenderTargets& targets);

private:
    void renderSsaoBase(const FrameContext& ctx, const SsaoSettings& ssao,
                        DeferredRenderTargets& targets);
    void renderSsaoFilter(const FrameContext& ctx, DeferredRenderTargets& targets);
    void renderSsaoUpsample(const FrameContext& ctx, const SsaoSettings& ssao,
                            DeferredRenderTargets& targets);
    void renderSsaoTemporal(const FrameContext& ctx, const SsaoSettings& ssao,
                            DeferredRenderTargets& targets);

    // Shaders (non-owning, loaded by ResourceMgr)
    Shader* m_ssaoShader = nullptr;
    Shader* m_ssaoFilterShader = nullptr;
    Shader* m_ssaoUpsampleShader = nullptr;
    Shader* m_ssaoTemporalShader = nullptr;

    RhiTextureHandle m_noiseTexture;
};

#endif // MECRAFT_SSAO_PASS_H
