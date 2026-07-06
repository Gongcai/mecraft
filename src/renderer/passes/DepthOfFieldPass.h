#ifndef MECRAFT_DEPTH_OF_FIELD_PASS_H
#define MECRAFT_DEPTH_OF_FIELD_PASS_H

#include "RenderPass.h"
#include "../core/FrameContext.h"
#include "../core/RenderSettings.h"

#include <cstdint>

class DeferredRenderTargets;
class ResourceMgr;
class Shader;

/// Depth of field pass: circle-of-confusion based blur.
class DepthOfFieldPass : public RenderPass {
public:
    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "DepthOfField"; }

    void execute(const FrameContext& ctx, const RenderSettings& settings,
                 DeferredRenderTargets& targets);

private:
    Shader* m_dofShader = nullptr;
    uint32_t m_noiseTexture = 0;
};

#endif // MECRAFT_DEPTH_OF_FIELD_PASS_H
