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
class DeferredRenderTargets;
class ShadowTargets;
class GameplaySkyRenderer;
class WorldRenderBuffer;
class ChunkMeshingService;
class ThreadPool;
class ForwardPipeline;
class DeferredPipeline;

struct BlockTargetRenderData;
struct BlockBreakRenderData;

namespace ecs { class GameplayRegistry; }
namespace shadow { class ShadowRenderer; }

/// Shared render resources used by both pipelines.
/// All pointers are non-owning; lifetime is managed by Renderer.
struct SharedRenderResources {
    // Terrain
    TerrainRenderCache* terrainCache = nullptr;
    TerrainRenderer* terrain = nullptr;
    WorldRenderBuffer* worldRenderBuffer = nullptr;
    ChunkMeshingService* meshingService = nullptr;

    // Render targets
    CommonFrameTargets* commonTargets = nullptr;
    DeferredRenderTargets* deferredTargets = nullptr;
    ShadowTargets* shadowTargets = nullptr;

    // Renderers
    GameplaySkyRenderer* sky = nullptr;
    shadow::ShadowRenderer* shadowRenderer = nullptr;
    ResourceMgr* resources = nullptr;
    ThreadPool* threadPool = nullptr;

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

    // State
    void setEyeInWater(bool inWater);
    void setRenderLocalPlayerModel(bool visible);
    void setHeldBlockLightValue(int value);

    // Rendering (Phase R1: bridge to legacy Renderer)
    void renderOpaqueAndCutout(const World& world, const Camera& camera, const Window& window);
    void renderTransparentAndOverlays(const World& world, const BlockTargetRenderData& target,
                                      const BlockBreakRenderData& blockBreak, const Window& window);
    void renderDeferredDebugOverlay(const Window& window);

    // Query (Phase R1: bridge to legacy Renderer)
    bool isDeferredFrameActive() const;
    GLuint gbufDepthTexture() const;
    GLuint weatherMaskTexture() const;

    // Debug query helpers (replaces direct RenderPipelineSettings access)
    bool isLightDebugActive() const;

    // Frame output access
    const FrameOutput& getLastFrameOutput() const;

    /// Access the shared post-process pass.
    /// Used by Dashboard for exposure diagnostics and by Game for legacy API.
    PostProcessPass& postProcessPass() { return m_postProcessPass; }
    const PostProcessPass& postProcessPass() const { return m_postProcessPass; }

    // Legacy compatibility (temporary bridge to existing Renderer)
    void setLegacyRenderer(Renderer* renderer);
    void syncFrameOutputFromLegacyRenderer();

    /// Build PostProcessEffects from current settings and world state.
    /// Replaces the ~70 line parameter assembly in Game::renderFrame().
    PostProcessEffects buildPostProcessEffects(const World& world, const Camera& camera,
                                                const Window& window, float cameraRainVisibility,
                                                float screenRollRadians) const;

    /// Get held item shadow data from the last frame output.
    const FirstPersonShadowData& getHeldItemShadowData() const { return m_lastFrameOutput.heldItemShadow; }

    /// Get camera rain visibility from the current frame context.
    float getCameraRainVisibility() const { return m_currentContext.cameraRainVisibility; }

    /// Pre-compute camera rain visibility for a given camera position.
    /// Used by Game before rain rendering.
    float computeCameraRainVisibility(const World& world, const glm::vec3& cameraPos) const;

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
    FrameContext m_previousContext;
    bool m_hasPreviousContext = false;
    uint64_t m_frameCounter = 0;
    bool m_eyeInWater = false;

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
