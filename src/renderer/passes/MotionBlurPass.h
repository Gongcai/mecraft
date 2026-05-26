#ifndef MECRAFT_MOTION_BLUR_PASS_H
#define MECRAFT_MOTION_BLUR_PASS_H

#include "RenderPass.h"
#include "../core/FrameContext.h"
#include "../core/RenderSettings.h"

class DeferredRenderTargets;
class ResourceMgr;
class Shader;

/// Motion blur pass: velocity-based per-pixel blur.
class MotionBlurPass : public RenderPass {
public:
    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "MotionBlur"; }

    void execute(const FrameContext& ctx, const RenderSettings& settings,
                 DeferredRenderTargets& targets);

private:
    Shader* m_motionBlurShader = nullptr;
};

#endif // MECRAFT_MOTION_BLUR_PASS_H
