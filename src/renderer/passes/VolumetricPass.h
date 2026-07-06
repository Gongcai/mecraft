#ifndef MECRAFT_VOLUMETRIC_PASS_H
#define MECRAFT_VOLUMETRIC_PASS_H

#include "RenderPass.h"
#include "../core/FrameContext.h"
#include "../core/RenderSettings.h"

#include <cstdint>

class DeferredRenderTargets;
class ResourceMgr;
class Shader;

namespace shadow { class ShadowRenderer; }

/// Volumetric fog pass: ray-marched fog with temporal reprojection and scene composite.
class VolumetricPass : public RenderPass {
public:
    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "Volumetric"; }

    void setShadowRenderer(shadow::ShadowRenderer* sr) { m_shadowRenderer = sr; }

    /// Check if all required shaders are loaded.
    [[nodiscard]] bool hasShaders() const {
        return m_volumetricFogShader != nullptr && m_volumetricCompositeShader != nullptr;
    }

    /// Check if temporal shader is available.
    [[nodiscard]] bool hasTemporalShader() const { return m_volumetricTemporalShader != nullptr; }

    /// Execute volumetric fog march, temporal resolve, and composite.
    /// @param ctx Frame context
    /// @param settings Render settings
    /// @param targets Deferred render targets
    /// @param hasPreviousFrame Whether temporal history is available
    void execute(const FrameContext& ctx, const RenderSettings& settings,
                 DeferredRenderTargets& targets, bool hasPreviousFrame);
    void invalidateHistory();

private:
    [[nodiscard]] bool shouldRenderFog(const FrameContext& ctx, const RenderSettings& settings,
                                       bool hasPreviousFrame) const;
    void renderFog(const FrameContext& ctx, const RenderSettings& settings,
                   DeferredRenderTargets& targets);
    void renderTemporal(const FrameContext& ctx, const RenderSettings& settings,
                        DeferredRenderTargets& targets);
    void composite(const FrameContext& ctx, const RenderSettings& settings,
                   DeferredRenderTargets& targets, bool hasPreviousFrame);

    Shader* m_volumetricFogShader = nullptr;
    Shader* m_volumetricTemporalShader = nullptr;
    Shader* m_volumetricCompositeShader = nullptr;
    shadow::ShadowRenderer* m_shadowRenderer = nullptr;
    ResourceMgr* m_resourceMgr = nullptr;
    uint32_t m_noiseTexture = 0;
    bool m_hasRenderedFog = false;
    glm::vec3 m_lastCameraPos = glm::vec3(0.0f);
    float m_lastWeatherSignal = 0.0f;
};

#endif // MECRAFT_VOLUMETRIC_PASS_H
