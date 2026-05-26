#ifndef MECRAFT_VELOCITY_PASS_H
#define MECRAFT_VELOCITY_PASS_H

#include "RenderPass.h"
#include "../core/FrameContext.h"
#include "../core/RenderSettings.h"

class DeferredRenderTargets;
class ResourceMgr;
class Shader;

/// Screen-space velocity pass: reprojects depth to compute per-pixel velocity.
class VelocityPass : public RenderPass {
public:
    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "Velocity"; }

    void execute(const FrameContext& ctx, const RenderSettings& settings,
                 DeferredRenderTargets& targets);

private:
    Shader* m_velocityShader = nullptr;
};

#endif // MECRAFT_VELOCITY_PASS_H
