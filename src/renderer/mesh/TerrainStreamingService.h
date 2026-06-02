#ifndef MECRAFT_TERRAIN_STREAMING_SERVICE_H
#define MECRAFT_TERRAIN_STREAMING_SERVICE_H

#include "TerrainRenderCache.h"
#include "ChunkMeshingService.h"
#include <glm/glm.hpp>
#include <array>
#include <cstdint>

class IWorldView;
class World;
class ThreadPool;
class WorldRenderBuffer;

/// Centralized terrain mesh streaming orchestrator.
/// Owns TerrainRenderCache and ChunkMeshingService, manages meshing budgets,
/// and provides debug statistics for the terrain pipeline.
///
/// Call order per frame:
///   1. beginFrame()
///   2. releaseStaleMdiAllocations(world)
///   3. drainMeshingResults(world)
///   4. (pipeline renders terrain)
///   5. submitMeshingJobs(world, cameraPos)
///   6. endFrame()
class TerrainStreamingService {
public:
    void init(ThreadPool* threadPool, WorldRenderBuffer* worldRenderBuffer = nullptr);
    void shutdown();

    /// Reset per-frame counters. Call at start of frame.
    void beginFrame();

    /// Release GPU allocations for sub-chunks whose generation has changed.
    void releaseStaleMdiAllocations(const IWorldView& worldView);

    /// Release a specific MDI allocation by key.
    void releaseMdiAllocation(const SubChunkGpuKey& key);

    /// Drain completed meshing results and upload to GPU.
    void drainMeshingResults(const IWorldView& worldView);

    /// Submit new meshing jobs for dirty sub-chunks near the camera.
    void submitMeshingJobs(const IWorldView& worldView, const glm::vec3& cameraPos);

    /// Record meshing history for debug display. Call at end of frame.
    void endFrame();

    // Configuration
    void setMeshingSubmitBudget(int budget);
    void setRegionChunkSize(int chunkSize);

    // Accessors for sub-components
    [[nodiscard]] TerrainRenderCache& terrainCache() { return m_terrainCache; }
    [[nodiscard]] const TerrainRenderCache& terrainCache() const { return m_terrainCache; }
    [[nodiscard]] ChunkMeshingService& meshingService() { return m_meshingService; }
    [[nodiscard]] const ChunkMeshingService& meshingService() const { return m_meshingService; }

    // Configuration accessors
    [[nodiscard]] int meshingSubmitBudget() const { return m_meshingSubmitBudget; }
    [[nodiscard]] int regionChunkSize() const { return m_regionChunkSize; }

#ifdef MECRAFT_DEBUG
    static constexpr size_t MESHING_HISTORY_SIZE = 120;

    struct MeshingFrameStats {
        int submitBudget = 0;
        int submitted = 0;
        int completed = 0;
        int inFlight = 0;
        int staleDropped = 0;
        int deferredResults = 0;
        double lastBuildMs = 0.0;
        double averageBuildMs = 0.0;
        uint32_t lastOpaqueFacesBeforeGreedy = 0;
        uint32_t lastOpaqueFacesAfterGreedy = 0;
        uint32_t lastTransparentFacesBeforeGreedy = 0;
        uint32_t lastTransparentFacesAfterGreedy = 0;
        uint32_t lastOpaqueVertexCount = 0;
    };

    [[nodiscard]] MeshingFrameStats getMeshingFrameStats() const;
    [[nodiscard]] const std::array<float, MESHING_HISTORY_SIZE>& getMeshingSubmittedHistory() const { return m_meshingSubmittedHistory; }
    [[nodiscard]] const std::array<float, MESHING_HISTORY_SIZE>& getMeshingCompletedHistory() const { return m_meshingCompletedHistory; }
    [[nodiscard]] const std::array<float, MESHING_HISTORY_SIZE>& getMeshingInFlightHistory() const { return m_meshingInFlightHistory; }
    [[nodiscard]] size_t getMeshingHistoryCount() const { return m_meshingHistoryCount; }
#endif

private:
    void syncFrameStats();

    TerrainRenderCache m_terrainCache;
    ChunkMeshingService m_meshingService;

    // Meshing budget configuration
    int m_meshingSubmitBudget = 8;
    bool m_meshingSubmitBudgetOverridden = false;
    int m_meshingMaxInFlight = 16;
    double m_meshingSubmitTimeBudgetMs = 0.75;
    int m_meshingDrainBudget = 2;
    double m_meshingDrainTimeBudgetMs = 1.0;
    int m_meshingDrainVertexBudget = 65536;
    int m_regionChunkSize = 4;

#ifdef MECRAFT_DEBUG
    // Debug history (ring buffers)
    size_t m_meshingHistoryCount = 0;
    std::array<float, MESHING_HISTORY_SIZE> m_meshingSubmittedHistory{};
    std::array<float, MESHING_HISTORY_SIZE> m_meshingCompletedHistory{};
    std::array<float, MESHING_HISTORY_SIZE> m_meshingInFlightHistory{};
#endif
};

#endif // MECRAFT_TERRAIN_STREAMING_SERVICE_H
