#ifndef MECRAFT_TERRAIN_RENDERER_H
#define MECRAFT_TERRAIN_RENDERER_H

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <array>
#include <vector>

#include "TerrainRenderCache.h"
#include "WorldDrawBatch.h"
#include "../../world/chunk/SubChunk.h"

struct CascadeAabbCuller {
    glm::mat4 viewProj = glm::mat4(1.0f);
    float xyPaddingNdc = 0.0f;
    float zPaddingNdc = 0.0f;
    bool useZCulling = true;
    mutable int visibleCount = 0;
    mutable int culledCount = 0;
};

// Forward declarations for types used only as pointers/references in the public interface
class Chunk;
class IWorldView;
class World;
class Shader;
class ResourceMgr;
class WorldRenderBuffer;
class DeferredRenderTargets;

struct TextureArray;

namespace shadow { class ShadowCasterCuller; }

/// Entry describing a chunk or sub-chunk to render.
/// Redefined here to avoid circular dependency with Renderer.h.
struct ChunkRenderEntry {
    Chunk* chunk = nullptr;
    int scy = -1;        // -1 = column aggregate, otherwise sub-chunk index
    bool aggregated = false;
};

/// Settings subset required by bindChunkRenderState, decoupled from Renderer.
struct TerrainRenderSettings {
    bool rainWetSurfacesEnabled = true;
    bool rainSurfaceRipplesEnabled = true;
    bool aerialPerspectiveEnabled = true;
    bool volumetricLightEnabled = true;
    bool volumetricFogEnabled = true;
    float volumetricFogStrength = 1.0f;
    float directSunStrength = 1.0f;
    float skyAmbientStrength = 0.36f;
    float weatherSkylightScale = 1.0f;
    float minimumAmbient = 0.055f;
    float blockLightStrength = 1.0f;
    float fakeBounceStrength = 0.06f;
    float albedoDesaturation = 0.0f;
    float shadowDesaturation = 0.22f;
};

/// Sky lighting data required by bindChunkRenderState.
struct TerrainSkyLightingData {
    glm::vec3 cameraPos = glm::vec3(0.0f);
    glm::vec3 sunDirection = glm::vec3(0.0f);
    glm::vec3 moonDirection = glm::vec3(0.0f);
    glm::vec3 sunLightColor = glm::vec3(0.0f);
    glm::vec3 moonLightColor = glm::vec3(0.0f);
    glm::vec3 skyAmbientColor = glm::vec3(0.0f);
    glm::vec3 shadowTintColor = glm::vec3(0.0f);
    glm::vec3 horizonScatterColor = glm::vec3(0.0f);
    float skyIntensity = 1.0f;
    float moonVisibility = 0.0f;
    glm::vec3 directIlluminance = glm::vec3(0.0f);
    glm::vec3 skyIlluminance = glm::vec3(0.0f);
    glm::vec3 sunIlluminance = glm::vec3(0.0f);
    glm::vec3 moonIlluminance = glm::vec3(0.0f);
    glm::vec3 cloudDynamicWeather = glm::vec3(0.0f);
};

/// Fog parameters required by bindChunkRenderState.
struct TerrainFogData {
    bool enabled = true;
    int mode = 0;           // FogMode enum value
    glm::vec3 color = glm::vec3(0.67f, 0.84f, 1.0f);
    float start = 0.0f;
    float end = 1.0f;
    float density = 0.01f;
};

/// Atmosphere parameters required by bindChunkRenderState.
struct TerrainAtmosphereData {
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
    float directWeatherOcclusion = 1.0f;
    int directWeatherOcclusionOverride = 0;
};

/// Per-frame rendering data required by bindChunkRenderState.
/// Kept local to terrain rendering to avoid circular header dependencies.
struct TerrainFrameData {
    glm::mat4 view = glm::mat4(1.0f);
    glm::mat4 viewProj = glm::mat4(1.0f);
    glm::vec3 cameraPos = glm::vec3(0.0f);
    float animationTime = 0.0f;
    float shaderTime = 0.0f;
    float surfaceWetness = 0.0f;
    TerrainFogData fog{};
    TerrainSkyLightingData skyLighting{};
    TerrainAtmosphereData atmosphere{};
};

/// Extracts terrain rendering logic from Renderer.
/// Non-owning class: receives dependencies via pointers/references.
class TerrainRenderer {
public:
    using AabbVisibilityFn = bool (*)(const glm::vec3& boundsMin,
                                      const glm::vec3& boundsMax,
                                      void* userData);

    void init(ResourceMgr& resourceMgr);
    void shutdown();

    // --- Dependency injection ---
    void setWorldRenderBuffer(WorldRenderBuffer* buf) { m_worldRenderBuffer = buf; }
    void setTerrainRenderCache(TerrainRenderCache* cache) { m_terrainCache = cache; }

    // --- Per-frame state ---
    void updateFrustum(const glm::mat4& viewProj);
    void setCameraPos(const glm::vec3& pos) { m_cameraPos = pos; }

    // --- Chunk render state binding ---
    /// Binds ~30 uniforms and ~15 textures for the chunk GBuffer shader.
    /// Inlines bindFogUniforms, bindSkyLightingUniforms, bindAtmosphereUniforms, bindWaterEffectUniforms.
    void bindChunkRenderState(const TerrainFrameData& frame, const TextureArray& texArray,
                              Shader& shader, bool deferredFrameActive, int debugLightMode,
                              bool eyeInWater, int heldBlockLightValue,
                              DeferredRenderTargets& targets, ResourceMgr* resourceMgr,
                              bool volumetricFogShadersReady,
                              const TerrainRenderSettings& settings);

    /// Lightweight forward-only state binding - no deferred resources.
    /// Binds only: texture array, lightmap, biome colormap, fog, and sky intensity.
    /// Does NOT bind: skyCapture, atmosphereLut, shadow maps, SSAO, SSR, volumetric.
    void bindBasicForwardState(const TerrainFrameData& frame, const TextureArray& texArray,
                               Shader& shader, bool eyeInWater, int heldBlockLightValue,
                               ResourceMgr* resourceMgr, const TerrainRenderSettings& settings);

    // --- Main rendering methods ---
    /// Traverses chunk columns with hierarchical frustum culling.
    /// In MDI mode, adds draw ranges to WorldRenderBuffer.
    /// In non-MDI mode, issues direct draw calls for opaque geometry.
    /// Collects cutout and transparent entries for later passes.
    void renderOpaqueChunksAndCollectPasses(
        const IWorldView& worldView,
        std::vector<ChunkRenderEntry>& cutoutEntries,
        std::vector<ChunkRenderEntry>& transparentEntries,
        bool frustumCull = true,
        float maxCameraDistance = 0.0f,
        shadow::ShadowCasterCuller* shadowCuller = nullptr,
        AabbVisibilityFn extraAabbCuller = nullptr,
        void* extraAabbCullerUserData = nullptr);

    /// Renders cutout chunks (MDI: flushes cutout buffer; non-MDI: draws individual VAOs).
    void renderCutoutChunks(const std::vector<ChunkRenderEntry>& cutoutEntries,
                            Shader& chunkShader);

    // --- Transparent batch management ---
    void syncTransparentBatches();
    void clearTransparentBatches();
    [[nodiscard]] const std::vector<DrawBatchEntry>& transparentBatches() const;
    [[nodiscard]] const TransparentPassPlan& transparentPassPlan() const;

    /// Collect chunks/ranges for all 4 shadow cascades in a single scene traversal.
    void collectShadowChunks(
        const IWorldView& worldView,
        const glm::vec3& cameraPos,
        float maxShadowDistance,
        shadow::ShadowCasterCuller* shadowCuller,
        const std::array<CascadeAabbCuller, 4>& cascadeCullers,
        std::array<std::vector<GpuMeshRange>, 4>& outOpaqueRanges,
        std::array<std::vector<GpuMeshRange>, 4>& outCutoutRanges,
        std::array<std::vector<GpuMeshRange>, 4>& outTransparentRanges,
        std::array<std::vector<ChunkRenderEntry>, 4>& outOpaqueEntries,
        std::array<std::vector<ChunkRenderEntry>, 4>& outCutoutEntries,
        std::array<std::vector<ChunkRenderEntry>, 4>& outTransparentEntries
    );

    // --- Accessors ---
    [[nodiscard]] const std::vector<ChunkRenderColumnCache>& chunkRenderColumns() const;
    [[nodiscard]] bool useMultiDrawIndirect() const { return m_useMultiDrawIndirect; }
    void setUseMultiDrawIndirect(bool v) { m_useMultiDrawIndirect = v; }
    void setChunkCullingDebugEnabled(bool v) { m_chunkCullingDebugEnabled = v; }

    // --- Debug counters (per-frame, reset externally) ---
    void resetDebugCounters();
    [[nodiscard]] int drawCallCount() const { return m_drawCallCount; }
    [[nodiscard]] int regionTestsThisFrame() const { return m_regionTestsThisFrame; }
    [[nodiscard]] int regionPassedThisFrame() const { return m_regionPassedThisFrame; }
    [[nodiscard]] int columnTestsThisFrame() const { return m_columnTestsThisFrame; }
    [[nodiscard]] int columnPassedThisFrame() const { return m_columnPassedThisFrame; }
    [[nodiscard]] int chunkTestsThisFrame() const { return m_chunkTestsThisFrame; }
    [[nodiscard]] int chunkPassedThisFrame() const { return m_chunkPassedThisFrame; }
    [[nodiscard]] int chunkCulledThisFrame() const { return m_chunkCulledThisFrame; }
    [[nodiscard]] int cutoutCandidatesThisFrame() const { return m_cutoutCandidatesThisFrame; }
    [[nodiscard]] int cutoutSkippedByDistanceThisFrame() const { return m_cutoutSkippedByDistanceThisFrame; }
    [[nodiscard]] int mdiSubChunkTestsThisFrame() const { return m_mdiSubChunkTestsThisFrame; }
    [[nodiscard]] int mdiSubChunksCulledThisFrame() const { return m_mdiSubChunksCulledThisFrame; }
    [[nodiscard]] const std::array<int, 6>& chunkCulledByPlaneThisFrame() const { return m_chunkCulledByPlaneThisFrame; }

    // --- Configuration ---
    [[nodiscard]] float cutoutRenderDistanceChunks() const { return m_cutoutRenderDistanceChunks; }
    void setCutoutRenderDistanceChunks(float v) { m_cutoutRenderDistanceChunks = v; }
    [[nodiscard]] bool cutoutDistanceLimitEnabled() const { return m_cutoutDistanceLimitEnabled; }
    void setCutoutDistanceLimitEnabled(bool v) { m_cutoutDistanceLimitEnabled = v; }

private:
    // --- Frustum culling ---
    enum class FrustumPlane : size_t {
        Left = 0,
        Right = 1,
        Bottom = 2,
        Top = 3,
        Near = 4,
        Far = 5,
        Count = 6
    };

    struct Plane {
        glm::vec3 normal = glm::vec3(0.0f);
        float distance = 0.0f;
    };

    static constexpr FrustumPlane kPlaneFromIndex(size_t index) {
        return static_cast<FrustumPlane>(index);
    }

    /// Returns true if AABB intersects the view frustum.
    /// When culledPlane is non-null and the AABB is culled, writes the culling plane index.
    bool isChunkInFrustum(const glm::vec3& chunkMin, const glm::vec3& chunkMax,
                          FrustumPlane* culledPlane = nullptr) const;

    /// Records a cull event for debug statistics.
    void recordChunkCull(FrustumPlane plane, int count);

    // --- Inline helper: expand AABB ---
    static void expandBounds(glm::vec3& minBounds, glm::vec3& maxBounds, bool& hasBounds,
                             const glm::vec3& candidateMin, const glm::vec3& candidateMax);

    // --- Inline shader binding helpers (inlined from Renderer) ---
    static void bindFogUniforms(Shader& shader, const TerrainFogData& fog);
    static void bindSkyLightingUniforms(Shader& shader, const TerrainSkyLightingData& sky,
                                        const glm::vec3& cameraPos, int heldBlockLightValue);
    static void bindAtmosphereUniforms(Shader& shader, const TerrainAtmosphereData& atm);
    static void bindWaterEffectUniforms(Shader& shader, bool enabled, ResourceMgr* resourceMgr);

    // --- Non-owning dependencies ---
    WorldRenderBuffer* m_worldRenderBuffer = nullptr;
    TerrainRenderCache* m_terrainCache = nullptr;

    // --- Per-frame state ---
    glm::vec3 m_cameraPos{};
    glm::mat4 m_viewProj{1.0f};
    std::array<Plane, 6> m_frustumPlanes{};

    // --- Configuration ---
    bool m_useMultiDrawIndirect = true;
    bool m_chunkCullingDebugEnabled = false;
    float m_cutoutRenderDistanceChunks = 4.0f;
    bool m_cutoutDistanceLimitEnabled = true;

    // --- Debug counters (per-frame) ---
    int m_drawCallCount = 0;
    int m_regionTestsThisFrame = 0;
    int m_regionPassedThisFrame = 0;
    int m_columnTestsThisFrame = 0;
    int m_columnPassedThisFrame = 0;
    int m_chunkTestsThisFrame = 0;
    int m_chunkPassedThisFrame = 0;
    int m_chunkCulledThisFrame = 0;
    int m_cutoutCandidatesThisFrame = 0;
    int m_cutoutSkippedByDistanceThisFrame = 0;
    int m_mdiSubChunkTestsThisFrame = 0;
    int m_mdiSubChunksCulledThisFrame = 0;
    std::array<int, static_cast<size_t>(FrustumPlane::Count)> m_chunkCulledByPlaneThisFrame{};
};

#endif // MECRAFT_TERRAIN_RENDERER_H
