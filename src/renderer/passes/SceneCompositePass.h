#ifndef MECRAFT_SCENE_COMPOSITE_PASS_H
#define MECRAFT_SCENE_COMPOSITE_PASS_H

#include "RenderPass.h"
#include "../core/FrameContext.h"
#include "../core/RenderSettings.h"

class DeferredRenderTargets;
class ResourceMgr;
class Shader;

/// Scene composite pass: combines lighting, reflections, clouds, and atmosphere into final HDR scene.
class SceneCompositePass : public RenderPass {
public:
    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "SceneComposite"; }

    void execute(const FrameContext& ctx, const RenderSettings& settings,
                 DeferredRenderTargets& targets);

private:
    Shader* m_sceneCompositeShader = nullptr;
};

#endif // MECRAFT_SCENE_COMPOSITE_PASS_H
