#ifndef MECRAFT_TERRAIN_RENDER_CACHE_H
#define MECRAFT_TERRAIN_RENDER_CACHE_H

#include "../../world/chunk/SubChunk.h"
#include "../../world/chunk/Chunk.h"
#include "TerrainBlasCache.h"
#include "WorldDrawBatch.h"
#include "ChunkMeshingService.h"
#include <glm/glm.hpp>
#include <vector>
#include <array>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>

class Chunk;
class IWorldView;
class World;
class WorldRenderBuffer;
class RhiCommandList;
class RhiDevice;

/// Per-sub-chunk transparent bounds cache
struct TransparentSubChunkCache {
    glm::vec3 boundsMin = glm::vec3(0.0f);
    glm::vec3 boundsMax = glm::vec3(0.0f);
};

/// Tracks command/vertex counts for generic transparent vs water passes
struct TransparentPassPlan {
    size_t genericCommands = 0;
    size_t waterCommands = 0;
    uint64_t genericVertices = 0;
    uint64_t waterVertices = 0;

    void clear() {
        genericCommands = 0;
        waterCommands = 0;
        genericVertices = 0;
        waterVertices = 0;
    }

    [[nodiscard]] bool hasGeneric() const { return genericCommands > 0; }
    [[nodiscard]] bool hasWater() const { return waterCommands > 0; }
    [[nodiscard]] bool hasAny() const { return hasGeneric() || hasWater(); }
};

/// Cached per-column render state for hierarchical culling
struct ChunkRenderColumnCache {
    Chunk* chunk = nullptr;
    int64_t chunkKey = 0;
    int regionX = 0;
    int regionZ = 0;
    int chunkX = 0;
    int chunkZ = 0;
    glm::vec3 worldOffset = glm::vec3(0.0f);
    bool stateValid = false;
    bool columnHasBounds = false;
    glm::vec3 columnBoundsMin = glm::vec3(0.0f);
    glm::vec3 columnBoundsMax = glm::vec3(0.0f);
    uint64_t chunkRenderStateRevision = 0;
    std::array<uint64_t, Chunk::NUM_SUB_CHUNKS> subChunkMeshRevisions{};
    std::array<uint64_t, Chunk::NUM_SUB_CHUNKS> subChunkMeshFingerprints{};
    std::array<int, Chunk::NUM_SUB_CHUNKS> transparentScys{};
    std::array<TransparentSubChunkCache, Chunk::NUM_SUB_CHUNKS> transparentSubChunks{};
    // Stable per-sub-chunk render state consumed by the main terrain traversal.
    std::array<uint8_t, Chunk::NUM_SUB_CHUNKS> subChunkRenderable{};
    std::array<glm::vec3, Chunk::NUM_SUB_CHUNKS> subChunkBoundsMin{};
    std::array<glm::vec3, Chunk::NUM_SUB_CHUNKS> subChunkBoundsMax{};
    std::array<WorldGpuMesh, Chunk::NUM_SUB_CHUNKS> subChunkMeshes{};
    int renderableCount = 0;
    int transparentCount = 0;
    uint64_t validatedFrameSerial = 0;
};

/// Tracks GPU memory allocation for a sub-chunk in the global pool
struct MdiMeshAllocation {
    WorldGpuMesh mesh;
};

/// Manages terrain mesh lifecycle: chunk column cache, MDI allocations, meshing jobs, transparent batches.
/// Does NOT perform any GL draw calls — Renderer retains drawing responsibility.
class TerrainRenderCache {
public:
    [[nodiscard]] bool init(RhiDevice* rhiDevice = nullptr);
    void shutdown();
    void beginFrame();

    // Configuration injection
    void setWorldRenderBuffer(WorldRenderBuffer* buf) { m_worldRenderBuffer = buf; }
    void setChunkMeshingService(ChunkMeshingService* svc) { m_meshingService = svc; }
    void setRegionChunkSize(int size) { m_regionChunkSize = size; }
    void setMeshingBudgets(int submitBudget, int maxInFlight, float submitTimeBudgetMs, int drainBudget,
                           float drainTimeBudgetMs, int drainVertexBudget);

    // Chunk column cache
    void syncChunkRenderColumns(const IWorldView& worldView);
    void refreshChunkRenderColumnCache(ChunkRenderColumnCache& column);
    [[nodiscard]] const std::vector<ChunkRenderColumnCache>& chunkRenderColumns() const { return m_chunkRenderColumns; }
    [[nodiscard]] std::vector<ChunkRenderColumnCache>& chunkRenderColumns() { return m_chunkRenderColumns; }

    // MDI allocation management
    void releaseMdiAllocation(const SubChunkGpuKey& key);
    void releaseStaleMdiAllocations(const IWorldView& worldView);
    [[nodiscard]] const std::unordered_map<SubChunkGpuKey, MdiMeshAllocation, SubChunkGpuKeyHash>&
    mdiMeshAllocations() const {
        return m_mdiMeshAllocations;
    }
    [[nodiscard]] std::unordered_map<SubChunkGpuKey, MdiMeshAllocation, SubChunkGpuKeyHash>& mdiMeshAllocations() {
        return m_mdiMeshAllocations;
    }

    // Meshing job management
    void submitMeshingJobs(const IWorldView& worldView, const glm::vec3& cameraPos);
    [[nodiscard]] bool drainMeshingResults(const IWorldView& worldView, RhiCommandList& commandList);
    void finishGraphExecution(bool succeeded, RhiSubmissionToken completionToken);
    [[nodiscard]] const std::unordered_set<int64_t>& meshingInFlight() const { return m_meshingInFlight; }
    [[nodiscard]] bool isMeshingSettled(const IWorldView& worldView) const;
    [[nodiscard]] TerrainBlasCache& blasCache() { return m_blasCache; }
    [[nodiscard]] const TerrainBlasCache& blasCache() const { return m_blasCache; }
    /// Returns a non-zero revision advanced whenever resident raster terrain geometry changes.
    [[nodiscard]] uint64_t localShadowGeometryRevision() const { return m_localShadowGeometryRevision; }

    [[nodiscard]] int meshingSubmittedThisFrame() const { return m_meshingSubmittedThisFrame; }
    [[nodiscard]] int meshingCompletedThisFrame() const { return m_meshingCompletedThisFrame; }
    [[nodiscard]] int meshingStaleDroppedThisFrame() const { return m_meshingStaleDroppedThisFrame; }
    [[nodiscard]] float meshingBuildMsThisFrame() const { return m_meshingBuildMsThisFrame; }
    [[nodiscard]] int deferredMeshResultCount() const { return static_cast<int>(m_deferredMeshResults.size()); }
    [[nodiscard]] int meshUploadVerticesThisFrame() const { return m_meshUploadVerticesThisFrame; }
    [[nodiscard]] int meshUploadBytesThisFrame() const { return m_meshUploadBytesThisFrame; }
    [[nodiscard]] int meshUploadDeferredCount() const { return m_meshUploadDeferredCount; }
    [[nodiscard]] float worldBufferUploadMsThisFrame() const { return m_worldBufferUploadMsThisFrame; }
    [[nodiscard]] int worldBufferExpandCountThisFrame() const { return m_worldBufferExpandCountThisFrame; }
    [[nodiscard]] float lastMeshingBuildMs() const { return m_lastMeshingBuildMs; }
    [[nodiscard]] uint32_t lastOpaqueFacesBeforeGreedy() const { return m_lastOpaqueFacesBeforeGreedy; }
    [[nodiscard]] uint32_t lastOpaqueFacesAfterGreedy() const { return m_lastOpaqueFacesAfterGreedy; }
    [[nodiscard]] uint32_t lastTransparentFacesBeforeGreedy() const { return m_lastTransparentFacesBeforeGreedy; }
    [[nodiscard]] uint32_t lastTransparentFacesAfterGreedy() const { return m_lastTransparentFacesAfterGreedy; }
    [[nodiscard]] uint32_t lastOpaqueVertexCount() const { return m_lastOpaqueVertexCount; }

    // Transparent batch collection
    void addTransparentBatch(const GpuMeshRange& range, float distanceSq, TransparentBatchKind kind);
    void clearTransparentBatches();
    [[nodiscard]] const std::vector<DrawBatchEntry>& deferredTransparentBatch() const {
        return m_deferredTransparentBatch;
    }
    [[nodiscard]] std::vector<DrawBatchEntry>& deferredTransparentBatch() { return m_deferredTransparentBatch; }
    [[nodiscard]] const TransparentPassPlan& transparentPassPlan() const { return m_transparentPassPlan; }

    // Debug
    void recordMeshingHistory();

private:
    void releaseMdiAllocationOnly(const SubChunkGpuKey& key);
    void collectRetiredMdiAllocations();
    void advanceLocalShadowGeometryRevision();

    struct RetiredMdiAllocation {
        WorldGpuMesh mesh;
        RhiSubmissionToken completionToken;
    };

    // Chunk column cache
    std::vector<ChunkRenderColumnCache> m_chunkRenderColumns;
    uint64_t m_chunkRenderColumnsRevision = 0;
    int m_chunkRenderColumnsRegionSize = 0;
    uint64_t m_frameSerial = 0;

    // MDI allocation tracking
    std::unordered_map<SubChunkGpuKey, MdiMeshAllocation, SubChunkGpuKeyHash> m_mdiMeshAllocations;
    std::vector<RetiredMdiAllocation> m_retiredMdiAllocations;
    TerrainBlasCache m_blasCache;
    uint64_t m_lastMdiAllocationSweepActiveRevision = 0;
    bool m_mdiAllocationSweepInitialized = false;
    uint64_t m_localShadowGeometryRevision = 1u;

    // Meshing state
    std::unordered_set<int64_t> m_meshingInFlight;
    std::vector<SubChunkMeshingResult> m_deferredMeshResults;

    // Transparent batch collection
    std::vector<DrawBatchEntry> m_deferredTransparentBatch;
    TransparentPassPlan m_transparentPassPlan;

    // Dependencies (non-owning)
    WorldRenderBuffer* m_worldRenderBuffer = nullptr;
    ChunkMeshingService* m_meshingService = nullptr;
    RhiDevice* m_rhiDevice = nullptr;
    RhiSubmissionToken m_lastGraphCompletionToken;

    // Configuration
    int m_regionChunkSize = 8;
    // Meshing budgets
    int m_meshingSubmitBudget = 16;
    int m_meshingMaxInFlight = 64;
    float m_meshingSubmitTimeBudgetMs = 2.0f;
    int m_meshingDrainBudget = 16;
    float m_meshingDrainTimeBudgetMs = 2.0f;
    int m_meshingDrainVertexBudget = 100000;

    // Upload accounting (per-frame)
    int m_meshUploadVerticesThisFrame = 0;
    int m_meshUploadBytesThisFrame = 0;
    int m_meshUploadDeferredCount = 0;
    float m_worldBufferUploadMsThisFrame = 0.0f;
    int m_worldBufferExpandCountThisFrame = 0;

    // Debug counters (per-frame)
    int m_meshingSubmittedThisFrame = 0;
    int m_meshingCompletedThisFrame = 0;
    int m_meshingStaleDroppedThisFrame = 0;
    float m_meshingBuildMsThisFrame = 0.0f;
    float m_lastMeshingBuildMs = 0.0f;
    uint32_t m_lastOpaqueFacesBeforeGreedy = 0;
    uint32_t m_lastOpaqueFacesAfterGreedy = 0;
    uint32_t m_lastTransparentFacesBeforeGreedy = 0;
    uint32_t m_lastTransparentFacesAfterGreedy = 0;
    uint32_t m_lastOpaqueVertexCount = 0;

    // Debug history (ring buffers)
    static constexpr int kHistorySize = 120;
    std::array<int, kHistorySize> m_submittedHistory{};
    std::array<int, kHistorySize> m_completedHistory{};
    std::array<size_t, kHistorySize> m_inFlightHistory{};
    int m_historyIndex = 0;
};

#endif // MECRAFT_TERRAIN_RENDER_CACHE_H
