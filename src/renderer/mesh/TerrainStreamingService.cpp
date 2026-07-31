#include "TerrainStreamingService.h"
#include "../../world/IWorldView.h"

void TerrainStreamingService::init(ThreadPool* threadPool, WorldRenderBuffer* worldRenderBuffer) {
    m_terrainCache.init();
    if (worldRenderBuffer != nullptr) {
        m_terrainCache.setWorldRenderBuffer(worldRenderBuffer);
    }
    m_terrainCache.setChunkMeshingService(&m_meshingService);
    m_terrainCache.setRegionChunkSize(m_regionChunkSize);
    if (!m_meshingSubmitBudgetOverridden) {
        const int workerCount = std::max(1, threadPool->numWorkers());
        m_meshingSubmitBudget = std::max(4, workerCount);
        m_meshingMaxInFlight = std::max(16, workerCount * 2);
#ifndef MECRAFT_DEBUG
        m_meshingSubmitTimeBudgetMs = 1.25;
        m_meshingDrainBudget = 8;
        m_meshingDrainTimeBudgetMs = 2.0;
#else
        m_meshingSubmitTimeBudgetMs = 0.75;
        m_meshingDrainBudget = 4;
        m_meshingDrainTimeBudgetMs = 1.25;
#endif
    }
    m_terrainCache.setMeshingBudgets(m_meshingSubmitBudget, m_meshingMaxInFlight,
                                     static_cast<float>(m_meshingSubmitTimeBudgetMs), m_meshingDrainBudget,
                                     static_cast<float>(m_meshingDrainTimeBudgetMs), m_meshingDrainVertexBudget);
    m_meshingService.start(threadPool);
}

void TerrainStreamingService::shutdown() {
    m_meshingService.shutdown();
    m_terrainCache.shutdown();
}

void TerrainStreamingService::beginFrame() {
    m_terrainCache.beginFrame();
}

void TerrainStreamingService::releaseStaleMdiAllocations(const IWorldView& worldView) {
    m_terrainCache.releaseStaleMdiAllocations(worldView);
}

void TerrainStreamingService::releaseMdiAllocation(const SubChunkGpuKey& key) {
    m_terrainCache.releaseMdiAllocation(key);
}

void TerrainStreamingService::drainMeshingResults(const IWorldView& worldView, RhiCommandList& commandList) {
    m_terrainCache.drainMeshingResults(worldView, commandList);
    syncFrameStats();
}

void TerrainStreamingService::submitMeshingJobs(const IWorldView& worldView, const glm::vec3& cameraPos) {
    m_terrainCache.submitMeshingJobs(worldView, cameraPos);
    syncFrameStats();
}

bool TerrainStreamingService::isSettled(const IWorldView& worldView) const {
    return m_terrainCache.isMeshingSettled(worldView);
}

void TerrainStreamingService::endFrame() {
#ifdef MECRAFT_DEBUG
    const int submitted = m_terrainCache.meshingSubmittedThisFrame();
    const int completed = m_terrainCache.meshingCompletedThisFrame();
    const size_t inFlight = m_terrainCache.meshingInFlight().size();

    if (m_meshingHistoryCount < MESHING_HISTORY_SIZE) {
        m_meshingSubmittedHistory[m_meshingHistoryCount] = static_cast<float>(submitted);
        m_meshingCompletedHistory[m_meshingHistoryCount] = static_cast<float>(completed);
        m_meshingInFlightHistory[m_meshingHistoryCount] = static_cast<float>(inFlight);
        ++m_meshingHistoryCount;
        return;
    }

    for (size_t i = 1; i < MESHING_HISTORY_SIZE; ++i) {
        m_meshingSubmittedHistory[i - 1] = m_meshingSubmittedHistory[i];
        m_meshingCompletedHistory[i - 1] = m_meshingCompletedHistory[i];
        m_meshingInFlightHistory[i - 1] = m_meshingInFlightHistory[i];
    }

    m_meshingSubmittedHistory[MESHING_HISTORY_SIZE - 1] = static_cast<float>(submitted);
    m_meshingCompletedHistory[MESHING_HISTORY_SIZE - 1] = static_cast<float>(completed);
    m_meshingInFlightHistory[MESHING_HISTORY_SIZE - 1] = static_cast<float>(inFlight);
#endif
}

void TerrainStreamingService::setMeshingSubmitBudget(int budget) {
    m_meshingSubmitBudget = std::max(1, budget);
    m_meshingSubmitBudgetOverridden = true;
    m_terrainCache.setMeshingBudgets(m_meshingSubmitBudget, m_meshingMaxInFlight,
                                     static_cast<float>(m_meshingSubmitTimeBudgetMs), m_meshingDrainBudget,
                                     static_cast<float>(m_meshingDrainTimeBudgetMs), m_meshingDrainVertexBudget);
}

void TerrainStreamingService::setRegionChunkSize(int chunkSize) {
    m_regionChunkSize = std::max(1, chunkSize);
    m_terrainCache.setRegionChunkSize(m_regionChunkSize);
}

void TerrainStreamingService::syncFrameStats() {
    // Per-frame upload stats are always synced (not debug-only)
    // Debug counters remain in TerrainRenderCache and are accessed via terrainCache()
}

#ifdef MECRAFT_DEBUG
TerrainStreamingService::MeshingFrameStats TerrainStreamingService::getMeshingFrameStats() const {
    MeshingFrameStats stats;
    stats.submitBudget = m_meshingSubmitBudget;
    stats.submitted = m_terrainCache.meshingSubmittedThisFrame();
    stats.completed = m_terrainCache.meshingCompletedThisFrame();
    stats.inFlight = static_cast<int>(m_terrainCache.meshingInFlight().size());
    stats.staleDropped = m_terrainCache.meshingStaleDroppedThisFrame();
    stats.deferredResults = m_terrainCache.deferredMeshResultCount();
    stats.lastBuildMs = m_terrainCache.lastMeshingBuildMs();
    stats.averageBuildMs = m_terrainCache.meshingCompletedThisFrame() > 0
                               ? (m_terrainCache.meshingBuildMsThisFrame() /
                                  static_cast<double>(m_terrainCache.meshingCompletedThisFrame()))
                               : 0.0;
    stats.lastOpaqueFacesBeforeGreedy = m_terrainCache.lastOpaqueFacesBeforeGreedy();
    stats.lastOpaqueFacesAfterGreedy = m_terrainCache.lastOpaqueFacesAfterGreedy();
    stats.lastTransparentFacesBeforeGreedy = m_terrainCache.lastTransparentFacesBeforeGreedy();
    stats.lastTransparentFacesAfterGreedy = m_terrainCache.lastTransparentFacesAfterGreedy();
    stats.lastOpaqueVertexCount = m_terrainCache.lastOpaqueVertexCount();
    return stats;
}
#endif
