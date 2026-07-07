#ifndef MECRAFT_DEFERRED_LIGHTING_PASS_H
#define MECRAFT_DEFERRED_LIGHTING_PASS_H

#include "RenderPass.h"
#include "../core/FrameContext.h"
#include "../core/RenderSettings.h"
#include "../rhi/RhiHandles.h"

#include <cstdint>

class DeferredRenderTargets;
class ResourceMgr;
class Shader;

namespace shadow { class ShadowRenderer; }

/// Deferred lighting pass: computes full-scene lighting from GBuffer, shadows, SSAO, and atmosphere.
class DeferredLightingPass : public RenderPass {
public:
    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "DeferredLighting"; }

    /// Inject the shadow renderer for CSM uniform binding.
    void setShadowRenderer(shadow::ShadowRenderer* sr) { m_shadowRenderer = sr; }

    /// Set the held block light value (from player inventory).
    void setHeldBlockLightValue(int value) { m_heldBlockLightValue = value; }

    void execute(const FrameContext& ctx, const RenderSettings& settings,
                 DeferredRenderTargets& targets);

private:
    Shader* m_deferredLightingShader = nullptr;
    shadow::ShadowRenderer* m_shadowRenderer = nullptr;
    ResourceMgr* m_resourceMgr = nullptr;
    int m_heldBlockLightValue = 0;
    RhiTextureHandle m_noiseTexture;
    RhiTextureHandle m_rippleNormalTexture;
};

#endif // MECRAFT_DEFERRED_LIGHTING_PASS_H
