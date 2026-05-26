#ifndef MECRAFT_CLOUD_PASS_H
#define MECRAFT_CLOUD_PASS_H

#include "RenderPass.h"
#include "../core/FrameContext.h"
#include "../core/RenderSettings.h"
#include <glad/glad.h>

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

private:
    Shader* m_cloudShader = nullptr;
    GLuint m_noiseTexture = 0;
};

#endif // MECRAFT_CLOUD_PASS_H
