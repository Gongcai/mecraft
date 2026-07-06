#ifndef MECRAFT_GBUFFER_PASS_H
#define MECRAFT_GBUFFER_PASS_H

#include "RenderPass.h"
#include "../core/FrameContext.h"

class DeferredRenderTargets;
class ResourceMgr;
class Shader;
class BlockEntityRenderer;
class HumanoidRenderer;
class DropRenderer;
class FallingBlockRenderer;
class DropSystem;
class IWorldView;
struct RenderSettings;

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
    void executeEntities(const IWorldView& worldView, const FrameContext& ctx,
                         const RenderSettings& settings,
                         DeferredRenderTargets& targets,
                         HumanoidRenderer* humanoidRenderer,
                         ecs::GameplayRegistry* gameplayRegistry,
                         bool renderLocalPlayerModel);

    /// Execute block entity GBuffer rendering.
    /// Prerequisites: GBuffer FBO already bound from terrain pass.
    void executeBlockEntities(const IWorldView& worldView, const FrameContext& ctx,
                              const RenderSettings& settings,
                              DeferredRenderTargets& targets,
                              BlockEntityRenderer* blockEntityRenderer);

    /// Execute drop GBuffer rendering.
    /// Prerequisites: GBuffer FBO bound, per-object velocity attached from entities.
    void executeDrops(const IWorldView& worldView, const FrameContext& ctx,
                      const RenderSettings& settings,
                      DeferredRenderTargets& targets,
                      DropRenderer* dropRenderer, DropSystem* dropSystem);

    /// Execute falling-block GBuffer rendering.
    /// Prerequisites: GBuffer FBO bound, per-object velocity attached (same as drops).
    void executeFallingBlocks(const IWorldView& worldView, const FrameContext& ctx,
                              const RenderSettings& settings,
                              DeferredRenderTargets& targets,
                              FallingBlockRenderer* fallingBlockRenderer,
                              ecs::GameplayRegistry* gameplayRegistry);

private:
    Shader* m_entityGBufferShader = nullptr;
};

#endif // MECRAFT_GBUFFER_PASS_H
