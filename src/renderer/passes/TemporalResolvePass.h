#ifndef MECRAFT_TEMPORAL_RESOLVE_PASS_H
#define MECRAFT_TEMPORAL_RESOLVE_PASS_H

#include "RenderPass.h"
#include "../core/FrameContext.h"
#include "../core/RenderSettings.h"

class DeferredRenderTargets;
class ResourceMgr;
class Shader;

/// TAA resolve pass: blends current frame with reprojected history.
class TemporalResolvePass : public RenderPass {
public:
    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "TemporalResolve"; }

    void execute(const FrameContext& ctx, const RenderSettings& settings,
                 DeferredRenderTargets& targets);

private:
    Shader* m_temporalResolveShader = nullptr;
};

#endif // MECRAFT_TEMPORAL_RESOLVE_PASS_H
