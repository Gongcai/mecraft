#ifndef MECRAFT_GBUFFER_PASS_H
#define MECRAFT_GBUFFER_PASS_H

#include "RenderPass.h"
#include "../core/FrameContext.h"
#include <glad/glad.h>

class DeferredRenderTargets;
class ResourceMgr;
class Shader;
class HumanoidRenderer;
class DropRenderer;
class DropSystem;
class World;

namespace ecs { class GameplayRegistry; }

/// GBuffer pass for entities and drops.
/// Terrain GBuffer is handled separately by the terrain rendering pipeline.
class GBufferPass : public RenderPass {
public:
    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "GBuffer"; }

    /// Execute entity GBuffer rendering.
    /// Prerequisites: GBuffer FBO already bound from terrain pass.
    void executeEntities(const World& world, const FrameContext& ctx,
                         DeferredRenderTargets& targets,
                         HumanoidRenderer* humanoidRenderer,
                         ecs::GameplayRegistry* gameplayRegistry,
                         bool renderLocalPlayerModel);

    /// Execute drop GBuffer rendering.
    /// Prerequisites: GBuffer FBO bound, per-object velocity attached from entities.
    void executeDrops(const World& world, const FrameContext& ctx,
                      DeferredRenderTargets& targets,
                      DropRenderer* dropRenderer, DropSystem* dropSystem);

private:
    Shader* m_entityGBufferShader = nullptr;
};

#endif // MECRAFT_GBUFFER_PASS_H
