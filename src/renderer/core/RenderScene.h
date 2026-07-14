#ifndef MECRAFT_RENDER_SCENE_H
#define MECRAFT_RENDER_SCENE_H

#include "RenderSettings.h"
#include "RenderPipeline.h"
#include "FrameContext.h"
#include "FrameOutput.h"
#include "../passes/PostProcessPass.h"
#include "../passes/Fsr1Pass.h"
#include "../mesh/TerrainStreamingService.h"
#include "../overlays/BlockInteractionOverlayRenderer.h"
#include "../debug/RenderDebugService.h"
#include "../gi/VoxelGiClipmap.h"

#include <memory>
#include <functional>
#include <optional>

// Forward declarations
class RenderResourceHub;
class ResourceMgr;
class RhiDevice;
class RhiCommandListPool;
class IWorldView;
class World;
class Camera;
class DayNightSystem;
class WeatherSystem;
class Window;
class BlockEntityRenderer;
class HumanoidRenderer;
class DropRenderer;
class FallingBlockRenderer;
class ParticleSystem;
class DropSystem;
class TerrainRenderCache;
class TerrainStreamingService;
class TerrainRenderer;
class TerrainRhiPipelineSet;
class DeferredRenderTargets;
class GameplaySkyRenderer;
class WorldRenderBuffer;
class ChunkMeshingService;
class ThreadPool;
class ForwardPipeline;
class DeferredPipeline;
class RainRenderer;
class FirstPersonHeldItemRenderer;
class Inventory;

struct BlockTargetRenderData;
struct BlockBreakRenderData;
struct FirstPersonHeldItemMotion;

namespace ecs { class GameplayRegistry; }
namespace shadow { class ShadowRenderer; }

/// Shared render resources used by both pipelines.
/// All pointers are non-owning; lifetime is managed by Renderer.
struct SharedRenderResources {
    // RHI
    RhiDevice* rhiDevice = nullptr;
    RhiCommandListPool* commandListPool = nullptr;

    // Terrain
    TerrainRenderCache* terrainCache = nullptr;
    TerrainStreamingService* terrainStreaming = nullptr;
    TerrainRenderer* terrain = nullptr;
    TerrainRhiPipelineSet* terrainRhiPipelines = nullptr;
    WorldRenderBuffer* worldRenderBuffer = nullptr;
    ChunkMeshingService* meshingService = nullptr;

    // Render targets
    DeferredRenderTargets* deferredTargets = nullptr;

    // Renderers
    GameplaySkyRenderer* sky = nullptr;
    shadow::ShadowRenderer* shadowRenderer = nullptr;
    BlockInteractionOverlayRenderer* overlayRenderer = nullptr;
    ResourceMgr* resources = nullptr;
    ThreadPool* threadPool = nullptr;

    // Sub-renderers (non-owning)
    BlockEntityRenderer* blockEntityRenderer = nullptr;
    HumanoidRenderer* humanoidRenderer = nullptr;
    DropRenderer* dropRenderer = nullptr;
    FallingBlockRenderer* fallingBlockRenderer = nullptr;
    ParticleSystem* particleSystem = nullptr;
    DropSystem* dropSystem = nullptr;
    ecs::GameplayRegistry* gameplayRegistry = nullptr;
};

/// High-level render request for one gameplay frame.
struct RenderGameplayFrameRequest {
    const IWorldView& worldView;
    const Camera& camera;
    Window& window;
    /// Immutable swapchain extent returned by acquireFrame() for this frame.
    int framebufferWidth = 1;
    int framebufferHeight = 1;
    const DayNightSystem& dayNightSystem;
    const WeatherSystem& weatherSystem;
    const BlockTargetRenderData& target;
    const BlockBreakRenderData& blockBreak;
    RainRenderer& rainRenderer;
    float frameTime = 0.0f;
    float screenRollRadians = 0.0f;
    FirstPersonHeldItemRenderer* firstPersonHeldItemRenderer = nullptr;
    const Inventory* firstPersonInventory = nullptr;
    const FirstPersonHeldItemMotion* firstPersonHeldItemMotion = nullptr;
    bool renderFirstPersonHeldItem = false;
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
    void renderFrame(const IWorldView& worldView, const Camera& camera, const Window& window,
                     const glm::ivec2& frameRenderSize, const glm::ivec2& frameOutputSize,
                     float frameAspectRatio,
                     const BlockTargetRenderData& target, const BlockBreakRenderData& blockBreak,
                     const DayNightSystem& dayNightSystem, const WeatherSystem& weatherSystem);

    /// Render a complete gameplay frame, including scene, precipitation, and post-process setup.
    void renderGameplayFrame(const RenderGameplayFrameRequest& request);

    // Pipeline management
    void setPipelineMode(PipelineMode mode);
    PipelineMode getPipelineMode() const;
    const char* activePipelineName() const;

    // Settings
    void setSettings(const RenderSettings& settings);
    const RenderSettings& getSettings() const;
    void setSettingsChangedCallback(std::function<void(const RenderSettings&)> callback);
    [[nodiscard]] bool isFsr1Supported() const { return m_fsr1Supported; }

    // Sub-renderer injection (temporary until ECS-driven)
    void setBlockEntityRenderer(BlockEntityRenderer* ber);
    void setHumanoidRenderer(HumanoidRenderer* hr);
    void setDropRenderer(DropRenderer* dr);
    void setFallingBlockRenderer(FallingBlockRenderer* fbr);
    void setParticleSystem(ParticleSystem* ps);
    void setDropSystem(DropSystem* ds);
    void setGameplayRegistry(ecs::GameplayRegistry* reg);

    // State
    void setEyeInWater(bool inWater);
    void setRenderLocalPlayerModel(bool visible);
    void setHeldBlockLightValue(int value);

    // R7: Legacy bridge methods removed — use renderFrame() instead

    // Query methods (now retrieve from active context)
    [[nodiscard]] SkyColorsData getSkyColors() const { return m_currentContext.skyColors; }
    [[nodiscard]] SkyIlluminanceData getSkyIlluminanceData() const { return m_currentContext.skyIlluminance; }
    [[nodiscard]] glm::vec3 getFogColor() const { return m_currentContext.fog.color; }

    // Debug query helpers
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
    [[nodiscard]] const std::optional<TemporalFrameInput>& temporalFrameInput() const {
        return m_temporalFrameInput;
    }

    /// Access the shared post-process pass.
    /// Used by Dashboard for exposure diagnostics and by Game for legacy API.
    PostProcessPass& postProcessPass() { return m_postProcessPass; }
    const PostProcessPass& postProcessPass() const { return m_postProcessPass; }

    /// Access the debug service for GPU timers and stats.
    RenderDebugService& debugService() { return m_debugService; }
    const RenderDebugService& debugService() const { return m_debugService; }
    [[nodiscard]] const VoxelGiClipmapStats& getVoxelGiClipmapStats() const;

    // Owned services accessors
    TerrainStreamingService& getTerrainStreamingService() { return m_terrainStreamingService; }
    const TerrainStreamingService& getTerrainStreamingService() const { return m_terrainStreamingService; }

    BlockInteractionOverlayRenderer& getOverlayRenderer() { return m_overlayRenderer; }
    const BlockInteractionOverlayRenderer& getOverlayRenderer() const { return m_overlayRenderer; }

    // Initialize shared resources without depending on legacy renderer
    void setupResources(
        ThreadPool* threadPool,
        RhiDevice* rhiDevice,
        RhiCommandListPool* commandListPool,
        TerrainRenderer* terrain,
        TerrainRhiPipelineSet* terrainRhiPipelines,
        WorldRenderBuffer* worldRenderBuffer,
        DeferredRenderTargets* deferredTargets,
        GameplaySkyRenderer* sky,
        shadow::ShadowRenderer* shadowRenderer,
        const RenderSettings& initialSettings);

    /// Build PostProcessEffects from current settings and world state.
    /// Replaces the ~70 line parameter assembly in Game::renderFrame().
    PostProcessEffects buildPostProcessEffects(const IWorldView& worldView, const Camera& camera,
                                                float frameAspectRatio, float cameraRainVisibility,
                                                float screenRollRadians,
                                                const DayNightSystem& dayNightSystem,
                                                const WeatherSystem& weatherSystem) const;

    /// Get held item shadow data from the last frame output.
    const FirstPersonShadowData& getHeldItemShadowData() const { return m_lastFrameOutput.heldItemShadow; }

    /// Get camera rain visibility from the current frame context.
    float getCameraRainVisibility() const { return m_currentContext.cameraRainVisibility; }

    /// Pre-compute camera rain visibility for a given camera position.
    /// Used by Game before rain rendering.
    float computeCameraRainVisibility(const IWorldView& worldView, const glm::vec3& cameraPos) const;

private:
    /// Build FrameContext from world state
    FrameContext buildFrameContext(const IWorldView& worldView, const Camera& camera, const Window& window,
                                   const glm::ivec2& frameRenderSize, const glm::ivec2& frameOutputSize,
                                   float frameAspectRatio,
                                   const DayNightSystem& dayNightSystem, const WeatherSystem& weatherSystem);
    glm::ivec2 internalRenderSize(const glm::ivec2& displaySize) const;
    [[nodiscard]] bool isFsr1RuntimeEnabled() const;

    /// Prepare active pipeline targets that FrameContext depends on.
    bool prepareFrameResources(const glm::ivec2& frameRenderSize);

    /// Invalidate temporal/history resources when pipeline changes
    void invalidateFrameHistory();
    void refreshTemporalFrameInput();

    // Configuration
    RenderSettings m_settings;
    std::function<void(const RenderSettings&)> m_settingsChangedCallback;

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
    Fsr1Pass m_fsr1Pass;
    bool m_fsr1Supported = false;

    // Frame state
    FrameOutput m_lastFrameOutput;
    FrameContext m_currentContext;
    FrameContext m_previousContext;
    std::optional<TemporalFrameInput> m_temporalFrameInput;
    bool m_hasPreviousContext = false;
    uint64_t m_frameCounter = 0;
    bool m_eyeInWater = false;
    bool m_renderLocalPlayerModel = false;

    // Pipeline implementations (Phase 9)
    std::unique_ptr<ForwardPipeline> m_forwardPipeline;
    std::unique_ptr<DeferredPipeline> m_deferredPipeline;
    RenderPipeline* m_activePipeline = nullptr;
    bool m_newPipelineActive = false; // Set to true when shared resources are fully populated
    bool m_activePipelineInitialized = false;

    // Sub-renderers (non-owning)
    BlockEntityRenderer* m_blockEntityRenderer = nullptr;
    HumanoidRenderer* m_humanoidRenderer = nullptr;
    DropRenderer* m_dropRenderer = nullptr;
    FallingBlockRenderer* m_fallingBlockRenderer = nullptr;
    ParticleSystem* m_particleSystem = nullptr;
    DropSystem* m_dropSystem = nullptr;
    ecs::GameplayRegistry* m_gameplayRegistry = nullptr;
};

#endif // MECRAFT_RENDER_SCENE_H
