#ifndef MECRAFT_CLOUD_PASS_H
#define MECRAFT_CLOUD_PASS_H

#include "RenderPass.h"
#include "../core/FrameContext.h"
#include "../core/RenderSettings.h"
#include "../rhi/RhiHandles.h"

#include <cstdint>

class DeferredRenderTargets;
class ResourceMgr;
class Shader;

/// Cloud pass: ray-marched volumetric clouds with temporal reprojection.
class CloudPass : public RenderPass {
public:
    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "Cloud"; }

    void execute(const FrameContext& ctx, const RenderSettings& settings,
                 DeferredRenderTargets& targets);
    void invalidateHistory();

private:
    [[nodiscard]] bool shouldRenderClouds(const FrameContext& ctx, const RenderSettings& settings);

    Shader* m_cloudShader = nullptr;
    RhiTextureHandle m_noiseTexture;
    bool m_hasRenderedClouds = false;
    glm::vec3 m_lastCameraPos = glm::vec3(0.0f);
    float m_lastWeatherSignal = 0.0f;
};

#endif // MECRAFT_CLOUD_PASS_H
