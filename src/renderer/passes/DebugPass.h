#ifndef MECRAFT_DEBUG_PASS_H
#define MECRAFT_DEBUG_PASS_H

#include "RenderPass.h"
#include "../core/FrameContext.h"
#include "../core/RenderSettings.h"
#include "../rhi/RhiHandles.h"

#include <cstdint>

class DeferredRenderTargets;
class ResourceMgr;
class Shader;

namespace shadow { class ShadowRenderer; }

/// Debug visualization pass: renders GBuffer/lighting/shadow/SSAO debug overlays.
/// Displays all intermediate render targets for visual inspection.
class DebugPass : public RenderPass {
public:
    void init(ResourceMgr& resourceMgr) override;
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
    Shader* m_deferredDebugShader = nullptr;
    RhiTextureHandle m_noiseTexture;
    shadow::ShadowRenderer* m_shadowRenderer = nullptr;
};

#endif // MECRAFT_DEBUG_PASS_H
