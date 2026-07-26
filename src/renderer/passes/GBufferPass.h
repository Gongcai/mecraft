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
class RhiCommandList;
struct RenderSettings;

namespace ecs { class GameplayRegistry; }

/// GBuffer pass for entities and drops.
/// Terrain GBuffer is handled separately by the terrain rendering pipeline.
class GBufferPass : public RenderPass {
public:
    void init(ResourceMgr& resourceMgr);
    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "GBuffer"; }

    /// Records entity GBuffer rendering into a graph-owned command list.
    /// @param commandList Recording command list supplied by the Render Graph.
    /// @param worldView Read-only world state used to prepare visible entities.
    /// @param ctx Current frame camera and renderer dependencies.
    /// @param settings Current temporal projection settings.
    /// @param targets Persistent GBuffer attachments and views.
    /// @param humanoidRenderer Optional entity renderer; no work is recorded when null.
    /// @param gameplayRegistry Optional entity registry paired with the renderer.
    /// @param renderLocalPlayerModel True when the local player belongs in the GBuffer.
    /// @return True when optional work was skipped or all commands were recorded.
    [[nodiscard]] bool executeEntities(RhiCommandList& commandList,
                                       const IWorldView& worldView,
                                       const FrameContext& ctx,
                                       const RenderSettings& settings,
                                       DeferredRenderTargets& targets,
                                       HumanoidRenderer* humanoidRenderer,
                                       ecs::GameplayRegistry* gameplayRegistry,
                                       bool renderLocalPlayerModel);

    /// Records block entity GBuffer rendering into a graph-owned command list.
    /// @param commandList Recording command list supplied by the Render Graph.
    /// @param worldView Read-only world state used to prepare block entities.
    /// @param ctx Current frame camera and renderer dependencies.
    /// @param settings Current temporal projection settings.
    /// @param targets Persistent GBuffer attachments and views.
    /// @param blockEntityRenderer Optional renderer; no work is recorded when null.
    /// @return True when optional work was skipped or all commands were recorded.
    [[nodiscard]] bool executeBlockEntities(RhiCommandList& commandList,
                                            const IWorldView& worldView,
                                            const FrameContext& ctx,
                                            const RenderSettings& settings,
                                            DeferredRenderTargets& targets,
                                            BlockEntityRenderer* blockEntityRenderer);

    /// Records dropped item and block GBuffer rendering with per-object velocity.
    /// @param commandList Recording command list supplied by the Render Graph.
    /// @param worldView Read-only world state used to prepare visible drops.
    /// @param ctx Current frame camera and renderer dependencies.
    /// @param settings Current temporal projection settings.
    /// @param targets Persistent GBuffer attachments and views.
    /// @param dropRenderer Optional dropped-object renderer.
    /// @param dropSystem Optional dropped-object simulation paired with the renderer.
    /// @return True when optional work was skipped or all commands were recorded.
    [[nodiscard]] bool executeDrops(RhiCommandList& commandList,
                                    const IWorldView& worldView,
                                    const FrameContext& ctx,
                                    const RenderSettings& settings,
                                    DeferredRenderTargets& targets,
                                    DropRenderer* dropRenderer,
                                    DropSystem* dropSystem);

    /// Records falling-block GBuffer rendering with per-object velocity.
    /// @param commandList Recording command list supplied by the Render Graph.
    /// @param worldView Read-only world state used to prepare falling blocks.
    /// @param ctx Current frame camera and renderer dependencies.
    /// @param settings Current temporal projection settings.
    /// @param targets Persistent GBuffer attachments and views.
    /// @param fallingBlockRenderer Optional falling-block renderer.
    /// @param gameplayRegistry Optional entity registry paired with the renderer.
    /// @return True when optional work was skipped or all commands were recorded.
    [[nodiscard]] bool executeFallingBlocks(RhiCommandList& commandList,
                                            const IWorldView& worldView,
                                            const FrameContext& ctx,
                                            const RenderSettings& settings,
                                            DeferredRenderTargets& targets,
                                            FallingBlockRenderer* fallingBlockRenderer,
                                            ecs::GameplayRegistry* gameplayRegistry);

};

#endif // MECRAFT_GBUFFER_PASS_H
