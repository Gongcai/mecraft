//
// Created by Caiwe on 2026/3/21.
//

#ifndef MECRAFT_RENDERER_H
#define MECRAFT_RENDERER_H
#include "engine/camera/Camera.h"
#include "../../resource/ResourceMgr.h"
#include "../../thread/ThreadPool.h"
#include "engine/platform/Window.h"
#include "../mesh/ChunkMeshingService.h"
#include "../targets/DeferredRenderTargets.h"

#include "../passes/GBufferPass.h"
#include <memory>
#include "../renderers/GameplaySkyRenderer.h"
#include "Shader.h"
#include "../shadow/ShadowRenderer.h"
#include "../mesh/TerrainRenderCache.h"
#include "../mesh/TerrainStreamingService.h"
#include "../mesh/TerrainRenderer.h"
#include "../mesh/WorldRenderBuffer.h"
#include "../mesh/WorldDrawBatch.h"
#include "../overlays/BlockInteractionOverlayRenderer.h"
#include "../debug/RenderDebugService.h"
#include "DeferredPipeline.h"
#include "RenderSettings.h"
#include <glm/glm.hpp>
#include <array>
#include <unordered_set>
#include <unordered_map>
#include <vector>

class World;
class Chunk;
class HumanoidRenderer;
class DropRenderer;
class ParticleSystem;
class DropSystem;

namespace ecs { class GameplayRegistry; }
namespace shadow { class ShadowCasterCuller; }

// BlockTargetRenderData and BlockBreakRenderData are now defined in BlockInteractionOverlayRenderer.h

class RenderResourceHub {
public:
    enum class FogMode : int {
        Linear = 0,
        Exp = 1,
        Exp2 = 2
    };

    struct FogSettings {
        bool enabled = true;
        FogMode mode = FogMode::Linear;
        glm::vec3 color = glm::vec3(0.67f, 0.84f, 1.0f);
        float startDistance = 140.0f;
        float endDistance = 260.0f;
        float density = 0.01f;
        bool autoDistanceByRenderDistance = true;
        float autoEndOffsetChunks = -0.5f;
        float autoFadeWidthChunks = 2.0f;
    };

    struct AtmosphereSettings {
        float aerialStrength = 0.65f;
        float horizonScatterStrength = 0.78f;
        float sunWarmth = 0.34f;
        float skyCoolness = 0.18f;
        float weatherWetness = 0.0f;
        float weatherStorm = 0.0f;
        float aerialReduction = 0.55f;
        float lightningFlash = 0.0f;
        float surfaceWetness = 0.0f;
        float skyWetness = 0.0f;
        float fogWetness = 0.0f;
        float cloudWetness = 0.0f;
        float precipitation = 0.0f;
        float directWeatherOcclusion = 1.0f; // 1.0=clear, 0.03=DerivativeMain Storm default
        int directWeatherOcclusionOverride = 0; // 0=auto, 1=manual bypass
    };

    struct VolumetricSettings {
        bool lightEnabled = true; // DerivativeMain VOLUMETRIC_LIGHT: base haze (airDensity)
        bool uwLightEnabled = true; // DerivativeMain UW_VOLUMETRIC_LIGHT: underwater volumetric light
        bool fogEnabled = true;
        float fogStrength = 1.0f;
        float underwaterLightStrength = 0.1f;
        float baseDensity = 1.0f;
        float heightFalloff = 0.022f;
        float maxDistance = 260.0f;
        // DerivativeMain-style VFog independent profile (decoupled from weather)
        float fogCenterHeight = 63.0f;   // SEA_LEVEL: y-level where fog is densest
        float fogHeightSpread = 100.0f;  // DerivativeMain falloff denominator: 100 -> exponent 0.01
        float fogNoiseScale = 0.04f;     // noise sampling scale for structured fog
        float fogLightStrength = 0.2f;   // DerivativeMain VOLUMETRIC_LIGHT_STRENGTH
        float fogDensityScale = 1.0f;    // user density multiplier (volFogDensity equivalent)
        int fogSamples = 20;             // DerivativeMain VOLUMETRIC_FOG_SAMPLES: march step count
    };

    struct CloudSettings {
        bool shadowsEnabled = true;
        float shadowStrength = 0.0f;
        float shadowScale = 0.0045f;
        float shadowSpeed = 0.018f;
        float timeScale = 0.35f;
        float coverage = 0.35f;
        float density = 1.0f;
        float height = 1000.0f;     // DerivativeMain CLOUD_CUMULUS_CLEAR_ALTITUDE
        float thickness = 1400.0f;  // DerivativeMain CLOUD_CUMULUS_CLEAR_THICKNESS
        // Planar clouds (cirrus)
        float planarCoverage = 0.5f;
        float planarDensity = 1.0f;
        float planarAltitude = 7000.0f; // DerivativeMain CLOUD_PLANE_ALTITUDE
    };

    // FrustumPlane is now defined in RenderDebugService.h

#ifdef MECRAFT_DEBUG
    // Type aliases for backward compatibility — actual definitions in TerrainStreamingService and RenderDebugService
    using MeshingFrameStats = TerrainStreamingService::MeshingFrameStats;
    // CullingFrameStats, GpuFrameStats, RenderWorkStats are now defined in RenderDebugService.h (global scope)

    // Constant alias for backward compatibility — actual definition in TerrainStreamingService
    static constexpr size_t MESHING_HISTORY_SIZE = TerrainStreamingService::MESHING_HISTORY_SIZE;
#endif

    ~RenderResourceHub();
    void init(ResourceMgr& resourceMgr);
    void shutdown();
    // R8: Rendering methods removed — use RenderScene::renderFrame() with DeferredPipeline/ForwardPipeline

    void setMeshingSubmitBudget(int budget);
    void setRegionChunkSize(int chunkSize);
    void setAtlasAnisotropy(float anisotropy);
    void setFogEnabled(bool enabled);
    void setFogMode(FogMode mode);
    void setFogColor(const glm::vec3& color);
    void setFogLinearDistances(float startDistance, float endDistance);
    void setFogDensity(float density);
    void setFogAutoDistanceEnabled(bool enabled);
    void setFogAutoEndOffsetChunks(float offsetChunks);
    void setFogAutoFadeWidthChunks(float fadeWidthChunks);
    [[nodiscard]] FogSettings getFogSettings() const;
    void setHeldBlockLightValue(int value);

    // Debug light visualization: 0=off, 1=sky light heatmap, 2=block light heatmap, 3=combined heatmap
    void setDebugLightMode(int mode);
    [[nodiscard]] int getDebugLightMode() const;
    void setSettings(const RenderSettings& settings);
    void setEyeInWater(bool inWater) { m_eyeInWater = inWater; }
    [[nodiscard]] const RenderSettings& getSettings() const;
    void setRenderLocalPlayerModel(bool visible) { m_renderLocalPlayerModel = visible; }
    void setHumanoidRenderer(HumanoidRenderer* hr) { m_humanoidRenderer = hr; }
    void setDropRenderer(DropRenderer* dr) { m_dropRenderer = dr; }
    void setParticleSystem(ParticleSystem* ps) { m_particleSystem = ps; }
    void setDropSystem(DropSystem* ds) { m_dropSystem = ds; }
    void setGameplayRegistry(ecs::GameplayRegistry* reg) { m_gameplayRegistry = reg; }
    // R8: renderDeferredDebugOverlay/isHybridDeferredReady removed — use RenderScene/DeferredPipeline
    // Shadow data for held item renderer — returns cascade matrices, textures, and settings.
    struct HeldItemShadowData {
        glm::mat4 cascadeViewProj[4]{};
        float cascadeSplitFar[4]{};
        float cascadeTexelWorldSize[4]{};
        GLuint shadowTexture = 0;
        GLuint shadowDepthRaw = 0;
        GLuint shadowDepthAll = 0;
        GLuint shadowDepthAllRaw = 0;
        GLuint shadowColor0 = 0;
        GLuint shadowColor1 = 0;
        glm::vec3 cameraPos = glm::vec3(0.0f);
        glm::vec3 sunDirection = glm::vec3(0.0f, 1.0f, 0.0f);
        float shadowDistance = 192.0f;
        float constantBias = 0.0007f;
        float slopeBias = 0.0022f;
        float normalOffset = 0.035f;
        float softness = 1.0f;
        float pcssStrength = 0.72f;
        int cascadeCount = 4;
        int softShadowsEnabled = 1;
        int pcssShadowsEnabled = 1;
        int shadowsEnabled = 1;
        float skyIntensity = 1.0f;
    };
    // R8: getHeldItemShadowData/gbufDepthTexture/weatherMaskTexture/bindWeatherMaskFbo removed — use RenderScene
    void restoreDefaultFbo();
    [[nodiscard]] ThreadPool* getThreadPool() { return &m_threadPool; }

    // Shared resource accessors (for RenderScene integration)
    [[nodiscard]] TerrainRenderer& getTerrainRenderer() { return m_terrainRenderer; }
    [[nodiscard]] GameplaySkyRenderer& getGameplaySkyRenderer() { return m_gameplaySkyRenderer; }
    [[nodiscard]] DeferredRenderTargets& getDeferredRenderTargets() { return m_deferredTargets; }
    [[nodiscard]] shadow::ShadowRenderer& getShadowRenderer() { return m_shadowRenderer; }
    [[nodiscard]] WorldRenderBuffer& getWorldRenderBuffer() { return m_worldRenderBuffer; }
    void setTerrainStreamingService(TerrainStreamingService* svc);
    void setOverlayRenderer(BlockInteractionOverlayRenderer* renderer) { m_overlayRenderer = renderer; }
    void setDebugService(RenderDebugService* svc) { m_debugService = svc; }
#ifdef MECRAFT_DEBUG
    void setChunkCullingDebugEnabled(bool enabled);
    [[nodiscard]] int getMeshingSubmitBudget() const;
    [[nodiscard]] int getRegionChunkSize() const;
    [[nodiscard]] bool isChunkCullingDebugEnabled() const;
    [[nodiscard]] MeshingFrameStats getMeshingFrameStats() const;
    [[nodiscard]] CullingFrameStats getCullingFrameStats() const;
    [[nodiscard]] GpuFrameStats getGpuFrameStats() const;
    [[nodiscard]] RenderWorkStats getRenderWorkStats() const;
    void setGpuTimerEnabled(bool enabled);
    [[nodiscard]] bool isGpuTimerEnabled() const;
    void setCutoutDistanceLimitEnabled(bool enabled);
    [[nodiscard]] bool isCutoutDistanceLimitEnabled() const;
    void setCutoutRenderDistanceChunks(float distanceChunks);
    [[nodiscard]] float getCutoutRenderDistanceChunks() const;
    [[nodiscard]] const std::array<float, MESHING_HISTORY_SIZE>& getMeshingSubmittedHistory() const;
    [[nodiscard]] const std::array<float, MESHING_HISTORY_SIZE>& getMeshingCompletedHistory() const;
    [[nodiscard]] const std::array<float, MESHING_HISTORY_SIZE>& getMeshingInFlightHistory() const;
    [[nodiscard]] size_t getMeshingHistoryCount() const;
#endif

    // 视锥剔除
    // R8: updateFrustum removed — use TerrainRenderer::updateFrustum
    [[nodiscard]] int getDrawCallCount() const;
    [[nodiscard]] bool isMultiDrawIndirectEnabled() const { return m_useMultiDrawIndirect; }
    [[nodiscard]] int getGlSubmitCount() const;
    [[nodiscard]] float getAtlasAnisotropy() const;
    [[nodiscard]] float getAtlasMaxAnisotropy() const;
private:
    struct Plane {
        glm::vec3 n = glm::vec3(0.0f);
        float d = 0.0f;
    };

    static constexpr int SHADOW_CASCADE_COUNT = shadow::ShadowRenderer::CASCADE_COUNT;
    using ShadowCascadeData = shadow::ShadowRenderer::Cascade;

    void recordMeshingHistory();
    void drawFullscreen(Shader& shader) const;
    glm::vec3 currentShadowLightDirection(const World& world, bool* moonShadowActive = nullptr) const;
    void captureCurrentFramebuffer();
    void restoreCapturedFramebufferViewport(const Window& window);
    void collectAndDrawOpaqueChunks(const World& world,
                                            std::vector<ChunkRenderEntry>& cutoutEntries,
                                            std::vector<ChunkRenderEntry>& transparentEntries,
                                            bool frustumCull = true,
                                            float maxCameraDistance = 0.0f,
                                            shadow::ShadowCasterCuller* shadowCuller = nullptr);
    void drawCutoutChunks(const std::vector<ChunkRenderEntry>& cutoutEntries);
    void drawTransparentChunks(const std::vector<ChunkRenderEntry>& transparentEntries);
    void executeTransparentShadowChunks(const std::vector<ChunkRenderEntry>& transparentEntries);
    void addTransparentBatch(const GpuMeshRange& range, float distanceSq, TransparentBatchKind kind);
    void syncChunkRenderColumns(const World& world);
    void refreshChunkRenderColumnCache(ChunkRenderColumnCache& column);
    void syncTerrainCacheFrameStats();
    void syncTerrainRendererFrameStats();
    void clearTransparentBatches();
    void syncTransparentBatches();
    void releaseStaleMdiAllocations(const World& world);
    void releaseMdiAllocation(const SubChunkGpuKey& key);
    // R8: Overlay methods removed — use BlockInteractionOverlayRenderer
#ifdef MECRAFT_DEBUG
    bool isChunkInFrustum(const glm::vec3& chunkMin, const glm::vec3& chunkMax, FrustumPlane* culledPlane) const;
    void recordChunkCull(FrustumPlane plane, int count);
    void initGpuTimers();
    void shutdownGpuTimers();
    void beginGpuTimerFrame();
    void beginGpuTimer(GpuTimerPass pass);
    void endGpuTimer(GpuTimerPass pass);
#endif
    bool isChunkInFrustum(const glm::vec3& chunkMin, const glm::vec3& chunkMax) const;
    //TODO: 传入 World 和 UI 数据进行渲染
    //void renderWorld(const World& world, const Camera& camera);
    //void renderUI(const UI& ui);

    int drawCallCount = 0;

    WorldRenderBuffer m_worldRenderBuffer;
    bool m_useMultiDrawIndirect = true;

    // Shaders actively used by Renderer rendering methods
    Shader* m_chunkShader = nullptr;
    Shader* m_chunkForwardShader = nullptr;
    Shader* m_transparentCompositeShader = nullptr;
    Shader* m_chunkGBufferShader = nullptr;
    Shader* m_particleGBufferShader = nullptr;
    Shader* m_shadowDepthShader = nullptr;
    Shader* m_outlineShader = nullptr;
    Shader* m_breakOverlayShader = nullptr;

    // Shaders used only for null-check guards (loaded but operations delegated to passes)
    Shader* m_entityGBufferShader = nullptr;
    Shader* m_entityShadowShader = nullptr;

    ResourceMgr* m_resourceMgr = nullptr;

    // R5: Overlay renderer — injected from RenderScene, null = use legacy members
    BlockInteractionOverlayRenderer* m_overlayRenderer = nullptr;
    // R6: Debug service — injected from RenderScene, null = use legacy members
    RenderDebugService* m_debugService = nullptr;

    GLuint m_outlineVao = 0;
    GLuint m_outlineVbo = 0;
    GLuint m_breakOverlayVao = 0;
    GLuint m_breakOverlayVbo = 0;
    GLsizei m_breakOverlayVertexCount = 0;
    GLuint m_breakOverlayCrossVao = 0;
    GLuint m_breakOverlayCrossVbo = 0;
    GLsizei m_breakOverlayCrossVertexCount = 0;

    ThreadPool m_threadPool;
    ChunkMeshingService m_meshingService;
    TerrainStreamingService* m_terrainStreamingService = nullptr;  // injected from RenderScene, null = use legacy members
    GameplaySkyRenderer m_gameplaySkyRenderer;
    HumanoidRenderer* m_humanoidRenderer = nullptr;  // injected from Game
    DropRenderer* m_dropRenderer = nullptr;  // injected from Game
    ParticleSystem* m_particleSystem = nullptr;  // injected from Game
    DropSystem* m_dropSystem = nullptr;  // injected from Game
    ecs::GameplayRegistry* m_gameplayRegistry = nullptr;  // injected from Game
    DeferredRenderTargets m_deferredTargets;
    RenderSettings m_settings;
    bool m_eyeInWater = false;
    int m_heldBlockLightValue = 0;
    GLint m_capturedFramebuffer = 0;
    GLint m_capturedViewport[4] = {0, 0, 0, 0};
    shadow::ShadowRenderer m_shadowRenderer;
    bool m_deferredFrameActive = false;
    bool m_renderLocalPlayerModel = true;
    bool m_waterRenderedBeforeTemporal = false;
    bool m_deferredHistoryUpdatedThisFrame = false;
    std::unordered_set<int64_t> m_meshingInFlight;
    std::vector<SubChunkMeshingResult> m_deferredMeshResults;
    int m_meshingSubmitBudget = 8;
    bool m_meshingSubmitBudgetOverridden = false;
    int m_meshingMaxInFlight = 16;
    double m_meshingSubmitTimeBudgetMs = 0.75;
    int m_meshingDrainBudget = 2;
    double m_meshingDrainTimeBudgetMs = 1.0;
    int m_meshingDrainVertexBudget = 65536;  // ~2MB vertex data per frame upload limit
    int m_regionChunkSize = 4;
#ifdef MECRAFT_DEBUG
    bool m_chunkCullingDebugEnabled = false;
    int m_meshingSubmittedThisFrame = 0;
    int m_meshingCompletedThisFrame = 0;
    int m_meshingStaleDroppedThisFrame = 0;
    double m_meshingBuildMsThisFrame = 0.0;
    double m_lastMeshingBuildMs = 0.0;
    uint32_t m_lastOpaqueFacesBeforeGreedy = 0;
    uint32_t m_lastOpaqueFacesAfterGreedy = 0;
    uint32_t m_lastTransparentFacesBeforeGreedy = 0;
    uint32_t m_lastTransparentFacesAfterGreedy = 0;
    uint32_t m_lastOpaqueVertexCount = 0;
    int m_regionTestsThisFrame = 0;
    int m_regionPassedThisFrame = 0;
    int m_columnTestsThisFrame = 0;
    int m_columnPassedThisFrame = 0;
    int m_chunkTestsThisFrame = 0;
    int m_chunkPassedThisFrame = 0;
    int m_chunkCulledThisFrame = 0;
    std::array<int, static_cast<size_t>(FrustumPlane::Count)> m_chunkCulledByPlaneThisFrame{};
    size_t m_meshingHistoryCount = 0;
    std::array<float, MESHING_HISTORY_SIZE> m_meshingSubmittedHistory{};
    std::array<float, MESHING_HISTORY_SIZE> m_meshingCompletedHistory{};
    std::array<float, MESHING_HISTORY_SIZE> m_meshingInFlightHistory{};
    static constexpr size_t GPU_TIMER_RING_SIZE = 4;
    std::array<std::array<GLuint, static_cast<size_t>(GpuTimerPass::Count)>, GPU_TIMER_RING_SIZE> m_gpuTimerQueries{};
    std::array<std::array<bool, static_cast<size_t>(GpuTimerPass::Count)>, GPU_TIMER_RING_SIZE> m_gpuTimerIssued{};
    GpuFrameStats m_gpuFrameStats{};
    size_t m_gpuTimerWriteIndex = 0;
    bool m_gpuTimersInitialized = false;
    bool m_gpuTimerEnabled = true;
    bool m_gpuTimerActive = false;
    bool m_gpuTimerCanIssueThisFrame = true;
    GpuTimerPass m_activeGpuTimerPass = GpuTimerPass::GBuffer;
    int m_cutoutCandidatesThisFrame = 0;
    int m_cutoutSkippedByDistanceThisFrame = 0;
    int m_mdiSubChunkTestsThisFrame = 0;
    int m_mdiSubChunksCulledThisFrame = 0;
#endif
    size_t m_meshUploadVerticesThisFrame = 0;
    size_t m_meshUploadBytesThisFrame = 0;
    size_t m_meshUploadDeferredCount = 0;
    size_t m_worldBufferExpandCountThisFrame = 0;
    double m_worldBufferUploadMsThisFrame = 0.0;

    glm::mat4 m_projection = glm::mat4(1.0f);
    glm::mat4 m_view = glm::mat4(1.0f);
    glm::mat4 m_viewProj = glm::mat4(1.0f);
    glm::vec3 m_cameraPos = glm::vec3(0.0f);
    float m_nearPlane = 0.1f;
    float m_farPlane = 500.0f;
    FogSettings m_fogSettings{};
    int m_debugLightMode = 0;
    bool m_cutoutDistanceLimitEnabled = true;
    float m_cutoutRenderDistanceChunks = 4.0f;
    // 视锥体6个平面
    std::array<Plane, 6> m_frustumPlanes{};
    TerrainRenderCache m_terrainCache;
    TerrainRenderer m_terrainRenderer;
    std::vector<ChunkRenderColumnCache> m_chunkRenderColumns;
    std::unordered_map<SubChunkGpuKey, MdiMeshAllocation, SubChunkGpuKeyHash> m_mdiMeshAllocations;
    std::vector<ChunkRenderEntry> m_deferredTransparentEntries;
    std::vector<DrawBatchEntry> m_deferredTransparentBatch;  // MDI path
    TransparentPassPlan m_transparentPassPlan;
    uint64_t m_chunkRenderColumnsRevision = 0;
    int m_chunkRenderColumnsRegionSize = 0;
};



#endif //MECRAFT_RENDERER_H
