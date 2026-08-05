#ifndef MECRAFT_TERRAIN_BLAS_CACHE_H
#define MECRAFT_TERRAIN_BLAS_CACHE_H

#include "TerrainGpuKey.h"
#include "renderer/contracts/TerrainRayTracingContract.h"
#include "renderer/rhi/RhiHandles.h"
#include "renderer/rhi/SceneBlasResource.h"
#include "renderer/rhi/RhiTypes.h"
#include "../../world/chunk/SubChunk.h"

#include <glm/vec3.hpp>

#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class RhiCommandList;
class RhiDevice;

/// Classifies a candidate geometry revision relative to the current resident revision.
enum class TerrainBlasRevisionRelation : uint8_t { Newer, Current, Stale };

/// Classifies one revision without mutating cache state.
/// @param hasCurrent True when currentRevision belongs to the same resident key lifetime.
/// @param currentRevision Latest accepted revision for the resident key lifetime.
/// @param candidateRevision Revision requested by the meshing producer.
/// @return Newer, Current, or Stale according to monotonic revision ordering.
[[nodiscard]] constexpr TerrainBlasRevisionRelation
terrainBlasClassifyRevision(const bool hasCurrent, const uint64_t currentRevision, const uint64_t candidateRevision) {
    if (!hasCurrent || candidateRevision > currentRevision) {
        return TerrainBlasRevisionRelation::Newer;
    }
    return candidateRevision == currentRevision ? TerrainBlasRevisionRelation::Current
                                                : TerrainBlasRevisionRelation::Stale;
}

/// Stable scheduler identity used to order queued terrain BLAS work.
struct TerrainBlasScheduleKey {
    uint64_t requestSequence = 0u;
    SubChunkGpuKey key;

    /// Orders requests by arrival and then by terrain identity for deterministic recording.
    [[nodiscard]] bool operator<(const TerrainBlasScheduleKey& other) const {
        if (requestSequence != other.requestSequence) {
            return requestSequence < other.requestSequence;
        }
        if (key.chunkKey != other.key.chunkKey) {
            return key.chunkKey < other.key.chunkKey;
        }
        return key.scy < other.key.scy;
    }
};

/// CPU geometry retained until a BLAS build submission is accepted.
struct TerrainBlasGeometry {
    std::vector<BlockVertex> vertices;
    std::vector<renderer::contracts::TerrainPrimitiveMetadata> primitiveMetadata;
    uint32_t opaqueVertexCount = 0u;
    uint32_t cutoutVertexCount = 0u;

    [[nodiscard]] uint32_t vertexCount() const { return opaqueVertexCount + cutoutVertexCount; }
    [[nodiscard]] uint32_t primitiveCount() const { return vertexCount() / 3u; }
    [[nodiscard]] uint64_t vertexByteSize() const {
        return static_cast<uint64_t>(vertices.size()) * sizeof(BlockVertex);
    }
    [[nodiscard]] uint64_t primitiveMetadataByteSize() const {
        return static_cast<uint64_t>(primitiveMetadata.size()) * sizeof(renderer::contracts::TerrainPrimitiveMetadata);
    }
    [[nodiscard]] uint64_t uploadByteSize() const { return vertexByteSize() + primitiveMetadataByteSize(); }
    [[nodiscard]] bool empty() const { return vertices.empty() && primitiveMetadata.empty(); }
};

// Expands only the ray-tracing copy of opaque terrain faces so perpendicular
// block faces overlap across floating-point and sub-chunk mesh boundaries.
inline constexpr float kTerrainBlasOpaqueSurfaceExpansion = 1.0f / 2048.0f;

/// Reports whether a terrain geometry request was accepted or rejected by its public contract.
enum class TerrainBlasRequestResult : uint8_t {
    Queued,
    Cleared,
    Unchanged,
    Unsupported,
    InvalidKey,
    InvalidRevision,
    StaleRevision,
    InvalidGeometry
};

/// Per-frame scheduler limits for terrain BLAS build and compaction work.
struct TerrainBlasBudgets {
    uint32_t maxBuilds = 4u;
    uint64_t maxBuildGeometryBytes = 8u * 1024u * 1024u;
    uint64_t maxBuildPrimitives = 262144u;
    uint32_t maxCompactions = 8u;
};

/// Read-only view of one compacted terrain BLAS generation.
struct TerrainBlasView {
    SubChunkGpuKey key;
    uint64_t revision = 0u;
    glm::vec3 worldOrigin = glm::vec3(0.0f);
    renderer::rt::SceneBlasResourcePtr resource;
    RhiAccelerationStructureHandle accelerationStructure;
    RhiBufferHandle geometryBuffer;
    RhiBufferHandle primitiveMetadataBuffer;
    uint64_t deviceAddress = 0u;
    uint64_t vertexAddress = 0u;
    uint64_t primitiveMetadataAddress = 0u;
    uint32_t opaqueVertexCount = 0u;
    uint32_t cutoutVertexCount = 0u;
    uint32_t primitiveCount = 0u;
    uint64_t geometryBytes = 0u;
    uint64_t primitiveMetadataBytes = 0u;
    uint64_t blasBytes = 0u;
    renderer::contracts::TerrainRayTracingHitData hitData;
};

/// Aggregated terrain BLAS residency and scheduler diagnostics.
struct TerrainBlasStats {
    bool supported = false;
    bool healthy = true;
    uint32_t activeBlasCount = 0u;
    uint32_t pendingBuildCount = 0u;
    uint32_t pendingCompactionCount = 0u;
    uint32_t retiredTaskCount = 0u;
    uint32_t buildsRecordedThisFrame = 0u;
    uint32_t compactionsRecordedThisFrame = 0u;
    uint64_t activePrimitiveCount = 0u;
    uint64_t activeGeometryBytes = 0u;
    uint64_t activePrimitiveMetadataBytes = 0u;
    uint64_t activeBlasBytes = 0u;
    uint64_t scratchPeakBytesThisFrame = 0u;
};

/// Builds, compacts, revisions, and retires per-SubChunk bottom-level acceleration structures.
class TerrainBlasCache {
public:
    /// Initializes the cache against one RHI device.
    /// @param device Device that owns every generated buffer, query, and acceleration structure.
    /// @return False only when an AS-capable device cannot create required cache resources.
    [[nodiscard]] bool init(RhiDevice* device);

    /// Releases every active and pending resource owned by the cache.
    void shutdown();

    /// Resolves completed submissions and resets per-frame recording counters.
    void beginFrame();

    /// Updates scheduler limits. Every field is clamped to at least one unit.
    void setBudgets(const TerrainBlasBudgets& budgets);

    /// Converts mesher output into one non-indexed BLAS geometry payload.
    /// @param opaque Opaque triangle-list vertices.
    /// @param cutout Alpha-tested triangle-list vertices without distance culling.
    /// @param cutoutDistance Alpha-tested triangle-list vertices with raster distance culling.
    /// @param geometry Receives sealed opaque vertices followed by unchanged cutout classes.
    /// @return Queued when valid, Cleared for empty solid geometry, or InvalidGeometry.
    [[nodiscard]] static TerrainBlasRequestResult prepareGeometry(const std::vector<BlockVertex>& opaque,
                                                                  const std::vector<BlockVertex>& cutout,
                                                                  const std::vector<BlockVertex>& cutoutDistance,
                                                                  TerrainBlasGeometry& geometry);

    /// Validates a terrain key against the fixed chunk-column subdivision.
    [[nodiscard]] static bool validKey(const SubChunkGpuKey& key);

    /// Queues a newer geometry generation while preserving the active BLAS until compaction completes.
    [[nodiscard]] TerrainBlasRequestResult requestBuild(const SubChunkGpuKey& key, uint64_t revision,
                                                        const glm::vec3& worldOrigin, TerrainBlasGeometry&& geometry);

    /// Removes one resident key and retires every generation still owned by it.
    void remove(const SubChunkGpuKey& key);

    /// Records budgeted build and compaction commands into the current graph command list.
    /// @return False when resource creation or command recording fails.
    [[nodiscard]] bool recordFrame(RhiCommandList& commandList);

    /// Commits recorded work to a submission token or rolls it back for exact retry.
    /// @param succeeded True when the render graph submitted every recorded command list.
    /// @param completionToken Last accepted graph submission, used for completion and safe query reuse.
    void finishGraphExecution(bool succeeded, RhiSubmissionToken completionToken);

    [[nodiscard]] bool supported() const { return m_supported; }
    [[nodiscard]] bool healthy() const { return m_healthy; }
    [[nodiscard]] bool isSettled() const;
    [[nodiscard]] const std::string& lastError() const { return m_lastError; }
    [[nodiscard]] std::optional<TerrainBlasView> activeView(const SubChunkGpuKey& key) const;
    /// Enumerates active BLAS generations ordered by chunk identity and vertical section.
    /// @return Stable views that retain every referenced BLAS and geometry buffer lifetime.
    [[nodiscard]] std::vector<TerrainBlasView> activeViews() const;
    [[nodiscard]] TerrainBlasStats stats() const;

private:
    enum class TaskState : uint8_t {
        Queued,
        BuildRecorded,
        BuildSubmitted,
        ReadyToCompact,
        CompactRecorded,
        CompactSubmitted
    };

    struct ActiveResource {
        uint64_t revision = 0u;
        glm::vec3 worldOrigin = glm::vec3(0.0f);
        renderer::rt::SceneBlasResourcePtr resource;
        RhiBufferHandle geometryBuffer;
        RhiBufferHandle primitiveMetadataBuffer;
        uint32_t opaqueVertexCount = 0u;
        uint32_t cutoutVertexCount = 0u;
        uint32_t primitiveCount = 0u;
        uint64_t geometryBytes = 0u;
        uint64_t primitiveMetadataBytes = 0u;
        renderer::contracts::TerrainRayTracingHitData hitData;
    };

    struct Entry {
        bool hasRevision = false;
        uint64_t latestRevision = 0u;
        uint64_t currentTaskSequence = 0u;
        std::optional<ActiveResource> active;
    };

    struct PendingTask {
        TerrainBlasScheduleKey schedule;
        uint64_t revision = 0u;
        glm::vec3 worldOrigin = glm::vec3(0.0f);
        TerrainBlasGeometry geometry;
        bool current = true;
        TaskState state = TaskState::Queued;
        RhiBufferHandle geometryBuffer;
        RhiBufferHandle primitiveMetadataBuffer;
        RhiBufferHandle buildStorageBuffer;
        RhiAccelerationStructureHandle buildAccelerationStructure;
        RhiBufferHandle scratchBuffer;
        RhiBufferHandle compactStorageBuffer;
        RhiAccelerationStructureHandle compactAccelerationStructure;
        uint64_t buildBlasBytes = 0u;
        uint64_t buildScratchBytes = 0u;
        uint64_t compactedBlasBytes = 0u;
        uint32_t queryIndex = std::numeric_limits<uint32_t>::max();
        RhiSubmissionToken submissionToken;
    };

    struct QuarantinedQuery {
        uint32_t queryIndex = 0u;
        RhiSubmissionToken completionToken;
    };

    static constexpr uint32_t kCompactedSizeQueryCapacity = 128u;
    static constexpr uint32_t kInvalidQueryIndex = std::numeric_limits<uint32_t>::max();

    [[nodiscard]] bool pollCompletedTasks();
    [[nodiscard]] bool recordBuild(PendingTask& task, RhiCommandList& commandList);
    [[nodiscard]] bool recordCompaction(PendingTask& task, RhiCommandList& commandList);
    [[nodiscard]] bool taskIsCurrent(const PendingTask& task) const;
    void retireCurrentTask(Entry& entry);
    [[nodiscard]] bool promoteTask(PendingTask& task, Entry& entry);
    void destroyTaskResources(PendingTask& task);
    void destroyBuildAttempt(PendingTask& task);
    void destroyCompactAttempt(PendingTask& task);
    void releaseQueryIndex(PendingTask& task);
    void quarantineQueryIndex(PendingTask& task, RhiSubmissionToken completionToken);
    [[nodiscard]] std::optional<uint32_t> acquireQueryIndex();
    void setTransientError(const char* message);
    void setFatalError(const char* message);

    RhiDevice* m_device = nullptr;
    RhiQueryPoolHandle m_compactedSizeQueries;
    bool m_initialized = false;
    bool m_supported = false;
    bool m_healthy = true;
    TerrainBlasBudgets m_budgets;
    uint64_t m_nextRequestSequence = 1u;
    std::unordered_map<SubChunkGpuKey, Entry, SubChunkGpuKeyHash> m_entries;
    std::map<uint64_t, PendingTask> m_tasks;
    std::vector<uint32_t> m_freeQueryIndices;
    std::vector<QuarantinedQuery> m_quarantinedQueries;
    std::vector<uint64_t> m_recordedBuilds;
    std::vector<uint64_t> m_recordedCompactions;
    uint32_t m_buildsRecordedThisFrame = 0u;
    uint32_t m_compactionsRecordedThisFrame = 0u;
    uint64_t m_scratchBytesRecordedThisFrame = 0u;
    uint64_t m_scratchPeakBytesThisFrame = 0u;
    std::string m_lastError;
};

#endif // MECRAFT_TERRAIN_BLAS_CACHE_H
