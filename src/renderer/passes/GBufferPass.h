#ifndef MECRAFT_GBUFFER_PASS_H
#define MECRAFT_GBUFFER_PASS_H

#include "RenderPass.h"
#include "../core/FrameContext.h"

class DeferredRenderTargets;
class ResourceMgr;
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
    void init(ResourceMgr& resourceMgr);
    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "GBuffer"; }

    /// Execute entity GBuffer rendering.
    /// Requires the active GBuffer rendering scope established by the terrain pass.
    void executeEntities(const IWorldView& worldView, const FrameContext& ctx,
                         const RenderSettings& settings,
                         DeferredRenderTargets& targets,
                         HumanoidRenderer* humanoidRenderer,
                         ecs::GameplayRegistry* gameplayRegistry,
                         bool renderLocalPlayerModel);

    /// Execute block entity GBuffer rendering.
    /// Requires the active GBuffer rendering scope established by the terrain pass.
    void executeBlockEntities(const IWorldView& worldView, const FrameContext& ctx,
                              const RenderSettings& settings,
                              DeferredRenderTargets& targets,
                              BlockEntityRenderer* blockEntityRenderer);

    /// Execute drop GBuffer rendering.
    /// Requires an active GBuffer rendering scope with the per-object velocity attachment.
    void executeDrops(const IWorldView& worldView, const FrameContext& ctx,
                      const RenderSettings& settings,
                      DeferredRenderTargets& targets,
                      DropRenderer* dropRenderer, DropSystem* dropSystem);

    /// Execute falling-block GBuffer rendering.
    /// Requires an active GBuffer rendering scope with the per-object velocity attachment.
    void executeFallingBlocks(const IWorldView& worldView, const FrameContext& ctx,
                              const RenderSettings& settings,
                              DeferredRenderTargets& targets,
                              FallingBlockRenderer* fallingBlockRenderer,
                              ecs::GameplayRegistry* gameplayRegistry);

};

#endif // MECRAFT_GBUFFER_PASS_H
