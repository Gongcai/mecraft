#ifndef MECRAFT_RENDER_SCENE_H
#define MECRAFT_RENDER_SCENE_H

#include "RenderSettings.h"
#include "RenderPipeline.h"
#include "FrameContext.h"
#include "FrameOutput.h"
#include "../passes/PostProcessPass.h"

#include <memory>

// Forward declarations
class Renderer;
class ResourceMgr;
class World;
class Camera;
class Window;
class HumanoidRenderer;
class DropRenderer;
class ParticleSystem;
class DropSystem;
class TerrainRenderCache;
class TerrainRenderer;
class CommonFrameTargets;
class DeferredFrameTargets;
class ShadowTargets;
class GameplaySkyRenderer;
class ForwardPipeline;
class DeferredPipeline;

struct BlockTargetRenderData;
struct BlockBreakRenderData;

namespace ecs { class GameplayRegistry; }

/// Shared render resources used by both pipelines
struct SharedRenderResources {
    // Terrain
    TerrainRenderCache* terrainCache = nullptr;
    TerrainRenderer* terrain = nullptr;

    // Render targets
    CommonFrameTargets* commonTargets = nullptr;
    DeferredFrameTargets* deferredTargets = nullptr;
    ShadowTargets* shadowTargets = nullptr;

    // Renderers
    GameplaySkyRenderer* sky = nullptr;
    ResourceMgr* resources = nullptr;

    // Sub-renderers (non-owning)
    HumanoidRenderer* humanoidRenderer = nullptr;
    DropRenderer* dropRenderer = nullptr;
    ParticleSystem* particleSystem = nullptr;
    DropSystem* dropSystem = nullptr;
};

/// Entry point for all rendering
/// Orchestrates pipeline selection, frame context building, and post-process
class RenderScene {
public:
    RenderScene();
    ~RenderScene();

    // Lifecycle
    void init(ResourceMgr& resourceMgr);
    void shutdown();

    /// Main render entry point (called from Game)
    void renderFrame(const World& world, const Camera& camera, const Window& window,
                     const BlockTargetRenderData& target, const BlockBreakRenderData& blockBreak);

    // Pipeline management
    void setPipelineMode(PipelineMode mode);
    PipelineMode getPipelineMode() const;
    const char* activePipelineName() const;

    // Settings
    void setSettings(const RenderSettings& settings);
    const RenderSettings& getSettings() const;

    // Sub-renderer injection (temporary until ECS-driven)
    void setHumanoidRenderer(HumanoidRenderer* hr);
    void setDropRenderer(DropRenderer* dr);
    void setParticleSystem(ParticleSystem* ps);
    void setDropSystem(DropSystem* ds);
    void setGameplayRegistry(ecs::GameplayRegistry* reg);

    // Frame output access
    const FrameOutput& getLastFrameOutput() const;

    /// Access the shared post-process pass.
    /// Used by Dashboard for exposure diagnostics and by Game for legacy API.
    PostProcessPass& postProcessPass() { return m_postProcessPass; }
    const PostProcessPass& postProcessPass() const { return m_postProcessPass; }

    // Legacy compatibility (temporary bridge to existing Renderer)
    void setLegacyRenderer(Renderer* renderer);

private:
    /// Build FrameContext from world state
    FrameContext buildFrameContext(const World& world, const Camera& camera, const Window& window);

    /// Invalidate temporal/history resources when pipeline changes
    void invalidateFrameHistory();

    // Configuration
    RenderSettings m_settings;

    // Shared infrastructure
    SharedRenderResources m_shared;

    // Shared post-processing pass (used by both Forward and Deferred pipelines)
    PostProcessPass m_postProcessPass;

    // Frame state
    FrameOutput m_lastFrameOutput;
    FrameContext m_currentContext;

    // Legacy bridge (Phase 1 only - will be removed when pipelines are extracted)
    Renderer* m_legacyRenderer = nullptr;

    // Pipeline implementations (Phase 9)
    std::unique_ptr<ForwardPipeline> m_forwardPipeline;
    std::unique_ptr<DeferredPipeline> m_deferredPipeline;
    RenderPipeline* m_activePipeline = nullptr;
    bool m_newPipelineActive = false; // Set to true when shared resources are fully populated

    // Sub-renderers (non-owning)
    HumanoidRenderer* m_humanoidRenderer = nullptr;
    DropRenderer* m_dropRenderer = nullptr;
    ParticleSystem* m_particleSystem = nullptr;
    DropSystem* m_dropSystem = nullptr;
    ecs::GameplayRegistry* m_gameplayRegistry = nullptr;
};

#endif // MECRAFT_RENDER_SCENE_H
