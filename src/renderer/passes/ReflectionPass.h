#ifndef MECRAFT_REFLECTION_PASS_H
#define MECRAFT_REFLECTION_PASS_H

#include "RenderPass.h"
#include "../core/FrameContext.h"
#include "../core/RenderSettings.h"

#include <cstdint>

class DeferredRenderTargets;
class ResourceMgr;
class Shader;

/// Reflection pass: SSR probe, bilateral filter, and temporal reprojection.
class ReflectionPass : public RenderPass {
public:
    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "Reflection"; }

    void execute(const FrameContext& ctx, const RenderSettings& settings,
                 DeferredRenderTargets& targets);

private:
    void renderReflection(const FrameContext& ctx, const RenderSettings& settings,
                          DeferredRenderTargets& targets);
    void renderFilter(const FrameContext& ctx, const ReflectionSettings& reflection,
                      DeferredRenderTargets& targets);
    void renderTemporal(const FrameContext& ctx, const ReflectionSettings& reflection,
                        DeferredRenderTargets& targets);

    Shader* m_reflectionShader = nullptr;
    Shader* m_reflectionFilterShader = nullptr;
    Shader* m_reflectionTemporalShader = nullptr;

    // Cached resource textures
    uint32_t m_noiseTexture = 0;
    uint32_t m_rippleNormalTexture = 0;
    ResourceMgr* m_resourceMgr = nullptr;
};

#endif // MECRAFT_REFLECTION_PASS_H
