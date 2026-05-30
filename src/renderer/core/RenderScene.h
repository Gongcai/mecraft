#ifndef MECRAFT_RENDER_SCENE_H
#define MECRAFT_RENDER_SCENE_H

#include "RenderSettings.h"
#include "RenderPipeline.h"
#include "FrameContext.h"
#include "FrameOutput.h"
#include "../passes/PostProcessPass.h"
#include "../mesh/TerrainStreamingService.h"
#include "../overlays/BlockInteractionOverlayRenderer.h"
#include "../debug/RenderDebugService.h"

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
class TerrainStreamingService;
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
    TerrainStreamingService* terrainStreaming = nullptr;
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
    BlockInteractionOverlayRenderer* overlayRenderer = nullptr;
    ResourceMgr* resources = nullptr;
    ThreadPool* threadPool = nullptr;

    // Sub-renderers (non-owning)
    HumanoidRenderer* humanoidRenderer = nullptr;
    DropRenderer* dropRenderer = nullptr;
    ParticleSystem* particleSystem = nullptr;
    DropSystem* dropSystem = nullptr;
    ecs::GameplayRegistry* gameplayRegistry = nullptr;
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

    /// Sync fog settings from Renderer to RenderSettings.
    /// Call this after Dashboard changes fog via Renderer methods.
    void syncFogFromRenderer();

    // R7: Legacy bridge methods removed — use renderFrame() instead

    // Query methods (now use FrameOutput directly)
    bool isDeferredFrameActive() const;
    GLuint gbufDepthTexture() const;
    GLuint weatherMaskTexture() const;

    // Debug query helpers (replaces direct RenderPipelineSettings access)
    bool isLightDebugActive() const;

    // Pipeline readiness (R2.6a)
    /// Check if the new pipeline path is ready to use.
    bool isNewPipelineReady() const;
    /// Enable or disable the new pipeline path (experimental).
    void setNewPipelineActive(bool active);
    /// Check whether the experimental new pipeline path is enabled.
    bool isNewPipelineActive() const { return m_newPipelineActive; }
    /// Get human-readable pipeline status string.
    const char* getPipelineStatus() const;

    // Frame output access
    const FrameOutput& getLastFrameOutput() const;

    /// Access the shared post-process pass.
    /// Used by Dashboard for exposure diagnostics and by Game for legacy API.
    PostProcessPass& postProcessPass() { return m_postProcessPass; }
    const PostProcessPass& postProcessPass() const { return m_postProcessPass; }

    /// Access the debug service for GPU timers and stats.
    RenderDebugService& debugService() { return m_debugService; }
    const RenderDebugService& debugService() const { return m_debugService; }

    // R8: Initialize shared resources from Renderer (terrain, sky, overlay, debug, etc.)
    void initFromRenderer(Renderer* renderer);

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

    /// Prepare active pipeline targets that FrameContext depends on.
    bool prepareFrameResources(const Window& window);

    /// Invalidate temporal/history resources when pipeline changes
    void invalidateFrameHistory();

    // Configuration
    RenderSettings m_settings;

    // Shared infrastructure
    SharedRenderResources m_shared;

    // Terrain streaming service (owned by RenderScene)
    TerrainStreamingService m_terrainStreamingService;

    // R5: Block interaction overlay renderer (owned by RenderScene)
    BlockInteractionOverlayRenderer m_overlayRenderer;

    // R6: Debug service (owned by RenderScene)
    RenderDebugService m_debugService;

    // Shared post-processing pass (used by both Forward and Deferred pipelines)
    PostProcessPass m_postProcessPass;

    // Frame state
    FrameOutput m_lastFrameOutput;
    FrameContext m_currentContext;
    FrameContext m_previousContext;
    bool m_hasPreviousContext = false;
    uint64_t m_frameCounter = 0;
    bool m_eyeInWater = false;
    bool m_renderLocalPlayerModel = false;

    // Renderer reference (for shared resource initialization)
    Renderer* m_sourceRenderer = nullptr;

    // Pipeline implementations (Phase 9)
    std::unique_ptr<ForwardPipeline> m_forwardPipeline;
    std::unique_ptr<DeferredPipeline> m_deferredPipeline;
    RenderPipeline* m_activePipeline = nullptr;
    bool m_newPipelineActive = false; // Set to true when shared resources are fully populated
    bool m_activePipelineInitialized = false;

    // Sub-renderers (non-owning)
    HumanoidRenderer* m_humanoidRenderer = nullptr;
    DropRenderer* m_dropRenderer = nullptr;
    ParticleSystem* m_particleSystem = nullptr;
    DropSystem* m_dropSystem = nullptr;
    ecs::GameplayRegistry* m_gameplayRegistry = nullptr;
};

#endif // MECRAFT_RENDER_SCENE_H
