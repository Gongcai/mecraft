#ifndef MECRAFT_SSGI_PASS_H
#define MECRAFT_SSGI_PASS_H

#include "RenderPass.h"
#include "../core/FrameContext.h"
#include "../core/RenderSettings.h"

#include <glad/glad.h>

class DeferredRenderTargets;
class ResourceMgr;
class Shader;

/// Screen-space global illumination pass: half-res gather, depth-aware upsample, temporal accumulation.
class SsgiPass : public RenderPass {
public:
    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "SSGI"; }

    void execute(const FrameContext& ctx, const RenderSettings& settings,
                 DeferredRenderTargets& targets);

private:
    void renderSsgiBase(const FrameContext& ctx, const RenderSettings& settings,
                        DeferredRenderTargets& targets);
    void renderSsgiUpsample(const FrameContext& ctx, DeferredRenderTargets& targets);
    void renderSsgiTemporal(const FrameContext& ctx, const SsgiSettings& ssgi,
                            DeferredRenderTargets& targets);

    Shader* m_ssgiShader = nullptr;
    Shader* m_ssgiUpsampleShader = nullptr;
    Shader* m_ssgiTemporalShader = nullptr;
    GLuint m_noiseTexture = 0;
};

#endif // MECRAFT_SSGI_PASS_H
