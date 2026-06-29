#include "TerrainRenderCache.h"
#include "WorldRenderBuffer.h"
#include "ChunkMesher.h"
#include "../../world/IWorldView.h"
#include "../../world/chunk/Chunk.h"
#include "../../world/chunk/SubChunk.h"
#include <algorithm>
#include <chrono>

namespace {

// Integer floor division: rounds toward negative infinity.
int floorDiv(const int value, const int divisor) {
    int q = value / divisor;
    const int r = value % divisor;
    if (r != 0 && ((r > 0) != (divisor > 0))) {
        --q;
    }
    return q;
}

// Candidate entry for distance-sorted meshing job submission.
struct MeshingCandidate {
    int64_t chunkKey = 0;
    Chunk* chunk = nullptr;
    int scy = 0;  // Sub-chunk index
    float distanceSq = 0.0f;
    std::shared_ptr<Chunk> chunkRef;
    std::shared_ptr<Chunk> neighborPosX;
    std::shared_ptr<Chunk> neighborNegX;
    std::shared_ptr<Chunk> neighborPosZ;
    std::shared_ptr<Chunk> neighborNegZ;
};

// Expand an AABB to include a candidate bounding box.
void expandBounds(glm::vec3& minBounds, glm::vec3& maxBounds, bool& hasBounds,
                  const glm::vec3& candidateMin, const glm::vec3& candidateMax) {
    if (!hasBounds) {
        minBounds = candidateMin;
        maxBounds = candidateMax;
        hasBounds = true;
        return;
    }

    minBounds.x = std::min(minBounds.x, candidateMin.x);
    minBounds.y = std::min(minBounds.y, candidateMin.y);
    minBounds.z = std::min(minBounds.z, candidateMin.z);
    maxBounds.x = std::max(maxBounds.x, candidateMax.x);
    maxBounds.y = std::max(maxBounds.y, candidateMax.y);
    maxBounds.z = std::max(maxBounds.z, candidateMax.z);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void TerrainRenderCache::init() {
    // No GPU resources to create; all state is CPU-side.
}

void TerrainRenderCache::beginFrame() {
    ++m_frameSerial;
    m_meshUploadVerticesThisFrame = 0;
    m_meshUploadBytesThisFrame = 0;
    m_meshUploadDeferredCount = static_cast<int>(m_deferredMeshResults.size());
    m_worldBufferUploadMsThisFrame = 0.0f;
    m_worldBufferExpandCountThisFrame = 0;

#ifdef MECRAFT_DEBUG
    m_meshingSubmittedThisFrame = 0;
    m_meshingCompletedThisFrame = 0;
    m_meshingStaleDroppedThisFrame = 0;
    m_meshingBuildMsThisFrame = 0.0f;
#endif
}

void TerrainRenderCache::shutdown() {
    m_chunkRenderColumns.clear();
    m_frameSerial = 0;
    m_mdiMeshAllocations.clear();
    m_mdiAllocationSweepInitialized = false;
    m_lastMdiAllocationSweepActiveRevision = 0;
    m_meshingInFlight.clear();
    m_deferredMeshResults.clear();
    m_deferredTransparentBatch.clear();
    m_transparentPassPlan.clear();
}

void TerrainRenderCache::setMeshingBudgets(const int submitBudget,
                                           const int maxInFlight,
                                           const float submitTimeBudgetMs,
                                           const int drainBudget,
                                           const float drainTimeBudgetMs,
                                           const int drainVertexBudget) {
    m_meshingSubmitBudget = std::max(1, submitBudget);
    m_meshingMaxInFlight = std::max(1, maxInFlight);
    m_meshingSubmitTimeBudgetMs = std::max(0.0f, submitTimeBudgetMs);
    m_meshingDrainBudget = std::max(1, drainBudget);
    m_meshingDrainTimeBudgetMs = std::max(0.0f, drainTimeBudgetMs);
    m_meshingDrainVertexBudget = std::max(1, drainVertexBudget);
}

// ---------------------------------------------------------------------------
// Chunk column cache
// ---------------------------------------------------------------------------

void TerrainRenderCache::syncChunkRenderColumns(const IWorldView& worldView) {
    const uint64_t activeChunkRevision = worldView.getActiveChunkRevision();
    const int regionChunkSize = std::max(1, m_regionChunkSize);
    if (m_chunkRenderColumnsRevision == activeChunkRevision &&
        m_chunkRenderColumnsRegionSize == regionChunkSize) {
        return;
    }

    const auto& activeChunks = worldView.getActiveChunks();
    m_chunkRenderColumns.clear();
    m_chunkRenderColumns.reserve(activeChunks.size());

    for (const auto& pair : activeChunks) {
        if (!pair.second) {
            continue;
        }

        ChunkRenderColumnCache column;
        column.chunk = pair.second.get();
        column.chunkKey = pair.first;
        column.chunkX = column.chunk->m_chunkX;
        column.chunkZ = column.chunk->m_chunkZ;
        column.regionX = floorDiv(column.chunkX, regionChunkSize);
        column.regionZ = floorDiv(column.chunkZ, regionChunkSize);
        column.worldOffset = glm::vec3(column.chunk->getWorldOffset());
        m_chunkRenderColumns.push_back(column);
    }

    std::sort(m_chunkRenderColumns.begin(), m_chunkRenderColumns.end(),
              [](const ChunkRenderColumnCache& a, const ChunkRenderColumnCache& b) {
                  if (a.regionX != b.regionX) {
                      return a.regionX < b.regionX;
                  }
                  if (a.regionZ != b.regionZ) {
                      return a.regionZ < b.regionZ;
                  }
                  if (a.chunkX != b.chunkX) {
                      return a.chunkX < b.chunkX;
                  }
                  return a.chunkZ < b.chunkZ;
              });

    m_chunkRenderColumnsRevision = activeChunkRevision;
    m_chunkRenderColumnsRegionSize = regionChunkSize;
}

void TerrainRenderCache::refreshChunkRenderColumnCache(ChunkRenderColumnCache& column) {
    if (column.chunk == nullptr) {
        return;
    }
    if (m_frameSerial != 0 && column.stateValid && column.validatedFrameSerial == m_frameSerial) {
        return;
    }

    const uint64_t renderStateRevision = column.chunk->getRenderStateRevision();
    if (column.stateValid && column.chunkRenderStateRevision == renderStateRevision) {
        column.validatedFrameSerial = m_frameSerial;
        return;
    }

    column.chunk->ensureColumnMeshBuilt();

    bool needsRefresh = !column.stateValid;
    for (int scy = 0; scy < Chunk::NUM_SUB_CHUNKS; ++scy) {
        const uint64_t revision = column.chunk->getSubChunkMeshRevision(scy);
        const SubChunk* sc = column.chunk->getSubChunk(scy);
        const uint64_t fingerprint = sc ? sc->getMesh().metadataFingerprint : 0ULL;
        if (!column.stateValid ||
            column.subChunkMeshRevisions[scy] != revision ||
            column.subChunkMeshFingerprints[scy] != fingerprint) {
            column.subChunkMeshRevisions[scy] = revision;
            column.subChunkMeshFingerprints[scy] = fingerprint;
            needsRefresh = true;
        }
    }

    if (!needsRefresh) {
        column.chunkRenderStateRevision = renderStateRevision;
        column.validatedFrameSerial = m_frameSerial;
        return;
    }

    const SubChunkMesh& columnMesh = column.chunk->getColumnMesh();
    column.aggregatedHasOpaque = columnMesh.vertexCount > 0;
    column.aggregatedHasCutout =
        columnMesh.cutoutVertexCount > 0 ||
        columnMesh.cutoutDistanceVertexCount > 0;
    column.aggregatedPresent = column.aggregatedHasOpaque || column.aggregatedHasCutout;

    const bool columnBoundsPresent = m_useMultiDrawIndirect
        ? columnMesh.hasBounds
        : column.aggregatedPresent;
    if (columnBoundsPresent) {
        column.aggregatedBoundsMin = columnMesh.hasBounds
            ? columnMesh.boundsMin
            : column.worldOffset;
        column.aggregatedBoundsMax = columnMesh.hasBounds
            ? columnMesh.boundsMax
            : column.worldOffset + glm::vec3(Chunk::SIZE_X, Chunk::SIZE_Y, Chunk::SIZE_Z);
    } else {
        column.aggregatedBoundsMin = glm::vec3(0.0f);
        column.aggregatedBoundsMax = glm::vec3(0.0f);
    }

    bool columnHasBounds = false;
    glm::vec3 columnMin(0.0f);
    glm::vec3 columnMax(0.0f);
    if (columnBoundsPresent) {
        expandBounds(columnMin, columnMax, columnHasBounds,
                     column.aggregatedBoundsMin, column.aggregatedBoundsMax);
    }

    column.transparentCount = 0;
    for (int scy = 0; scy < Chunk::NUM_SUB_CHUNKS; ++scy) {
        const SubChunk* sc = column.chunk->getSubChunk(scy);
        if (!sc) {
            continue;
        }

        const SubChunkMesh& mesh = sc->getMesh();
        if (mesh.transparentVertexCount == 0 && mesh.waterVertexCount == 0) {
            continue;
        }

        const int yBase = scy * SubChunk::SIZE;
        TransparentSubChunkCache& transparent = column.transparentSubChunks[scy];
        if (m_useMultiDrawIndirect) {
            transparent.boundsMin = mesh.hasBounds
                ? mesh.boundsMin
                : column.worldOffset + glm::vec3(0.0f, static_cast<float>(yBase), 0.0f);
            transparent.boundsMax = mesh.hasBounds
                ? mesh.boundsMax
                : column.worldOffset + glm::vec3(Chunk::SIZE_X, static_cast<float>(yBase + SubChunk::SIZE), Chunk::SIZE_Z);
        } else {
            transparent.boundsMin = mesh.hasBounds
                ? mesh.boundsMin + column.worldOffset
                : column.worldOffset + glm::vec3(0.0f, static_cast<float>(yBase), 0.0f);
            transparent.boundsMax = mesh.hasBounds
                ? mesh.boundsMax + column.worldOffset
                : column.worldOffset + glm::vec3(Chunk::SIZE_X, static_cast<float>(yBase + SubChunk::SIZE), Chunk::SIZE_Z);
        }

        column.transparentScys[column.transparentCount++] = scy;
        expandBounds(columnMin, columnMax, columnHasBounds,
                     transparent.boundsMin, transparent.boundsMax);
    }

    column.columnHasBounds = columnHasBounds;
    column.columnBoundsMin = columnHasBounds ? columnMin : glm::vec3(0.0f);
    column.columnBoundsMax = columnHasBounds ? columnMax : glm::vec3(0.0f);
    column.stateValid = true;
    column.chunkRenderStateRevision = renderStateRevision;
    column.validatedFrameSerial = m_frameSerial;
}

// ---------------------------------------------------------------------------
// MDI allocation management
// ---------------------------------------------------------------------------

void TerrainRenderCache::releaseMdiAllocation(const SubChunkGpuKey& key) {
    const auto it = m_mdiMeshAllocations.find(key);
    if (it == m_mdiMeshAllocations.end()) {
        return;
    }
    m_worldRenderBuffer->free(it->second.mesh);
    m_mdiMeshAllocations.erase(it);
}

void TerrainRenderCache::releaseStaleMdiAllocations(const IWorldView& worldView) {
    if (m_mdiMeshAllocations.empty()) {
        m_mdiAllocationSweepInitialized = true;
        m_lastMdiAllocationSweepActiveRevision = worldView.getActiveChunkRevision();
        return;
    }

    const uint64_t activeChunkRevision = worldView.getActiveChunkRevision();
    if (m_mdiAllocationSweepInitialized &&
        m_lastMdiAllocationSweepActiveRevision == activeChunkRevision) {
        return;
    }

    const auto& activeChunks = worldView.getActiveChunks();
    for (auto it = m_mdiMeshAllocations.begin(); it != m_mdiMeshAllocations.end(); ) {
        const auto chunkIt = activeChunks.find(it->first.chunkKey);
        bool release = (chunkIt == activeChunks.end() || !chunkIt->second);
        if (!release) {
            const SubChunk* sc = chunkIt->second->getSubChunk(it->first.scy);
            if (sc == nullptr || !sc->getMesh().inGlobalPool) {
                release = true;
            } else {
                const SubChunkMesh& current = sc->getMesh();
                release =
                    current.opaqueRange.generation != it->second.mesh.opaque.generation ||
                    current.cutoutRange.generation != it->second.mesh.cutout.generation ||
                    current.cutoutDistanceRange.generation != it->second.mesh.cutoutDistance.generation ||
                    current.transparentRange.generation != it->second.mesh.transparent.generation ||
                    current.waterRange.generation != it->second.mesh.water.generation;
            }
        }

        if (release) {
            m_worldRenderBuffer->free(it->second.mesh);
            it = m_mdiMeshAllocations.erase(it);
        } else {
            ++it;
        }
    }
    m_mdiAllocationSweepInitialized = true;
    m_lastMdiAllocationSweepActiveRevision = activeChunkRevision;
}

// ---------------------------------------------------------------------------
// Meshing job submission
// ---------------------------------------------------------------------------

void TerrainRenderCache::submitMeshingJobs(const IWorldView& worldView, const glm::vec3& cameraPos) {
    std::vector<MeshingCandidate> candidates;
    const auto& activeChunks = worldView.getActiveChunks();

    auto findSharedByCoords = [&](const int cx, const int cz) -> std::shared_ptr<Chunk> {
        const int64_t key = IWorldView::chunkKey(cx, cz);
        auto it = activeChunks.find(key);
        return (it != activeChunks.end()) ? it->second : nullptr;
    };

    // Build sub-chunk key for in-flight tracking: pack chunkKey + scy
    auto subChunkFlightKey = [](int64_t chunkKey, int scy) -> int64_t {
        return (chunkKey & 0x00FFFFFFFFFFFFFFLL) | (static_cast<int64_t>(scy) << 56);
    };

    auto clearSkippedSubChunkMesh = [&](Chunk& chunk, const int64_t chunkKey, const int scy) {
        const SubChunk* sc = chunk.getSubChunk(scy);
        if (!sc) {
            return;
        }

        releaseMdiAllocation(SubChunkGpuKey{chunkKey, scy});

        SubChunkMesh emptyMesh;
        chunk.setSubChunkMesh(scy, emptyMesh);

        ChunkMeshData emptyMeshData;
        if (m_useMultiDrawIndirect) {
            chunk.updateColumnAggregateBoundsOnly(scy, emptyMeshData, false);
        } else {
            chunk.updateColumnAggregateData(scy, emptyMeshData);
        }
    };

    for (const auto& pair : activeChunks) {
        const int64_t chunkKey = pair.first;
        Chunk& chunk = *pair.second;

        // Check each sub-chunk individually
        for (int scy = 0; scy < Chunk::NUM_SUB_CHUNKS; ++scy) {
            // Skip if not dirty
            if (!chunk.isSubChunkDirty(scy)) continue;

            // Skip if already in flight
            const int64_t flightKey = subChunkFlightKey(chunkKey, scy);
            if (m_meshingInFlight.find(flightKey) != m_meshingInFlight.end()) continue;

            // Air / fully occluded solid sub-chunks still need to replace any
            // previous mesh with an empty one; otherwise stale border faces
            // can survive after a neighbor loads.
            if (ChunkMesher::shouldSkipSubChunk(chunk, scy)) {
                clearSkippedSubChunkMesh(chunk, chunkKey, scy);
                continue;
            }

            const glm::ivec3 offset = chunk.getWorldOffset();
            const float centerX = static_cast<float>(offset.x) + Chunk::SIZE_X * 0.5f;
            const float centerZ = static_cast<float>(offset.z) + Chunk::SIZE_Z * 0.5f;
            const float dx = centerX - cameraPos.x;
            const float dz = centerZ - cameraPos.z;

            MeshingCandidate candidate;
            candidate.chunkKey = chunkKey;
            candidate.chunk = &chunk;
            candidate.scy = scy;
            candidate.distanceSq = dx * dx + dz * dz;
            candidate.chunkRef = pair.second;
            candidate.neighborPosX = findSharedByCoords(chunk.m_chunkX + 1, chunk.m_chunkZ);
            candidate.neighborNegX = findSharedByCoords(chunk.m_chunkX - 1, chunk.m_chunkZ);
            candidate.neighborPosZ = findSharedByCoords(chunk.m_chunkX, chunk.m_chunkZ + 1);
            candidate.neighborNegZ = findSharedByCoords(chunk.m_chunkX, chunk.m_chunkZ - 1);
            candidates.push_back(std::move(candidate));
        }
    }

    const int availableInFlightSlots = std::max(0, m_meshingMaxInFlight - static_cast<int>(m_meshingInFlight.size()));
    const int submitCount = std::min({m_meshingSubmitBudget, availableInFlightSlots, static_cast<int>(candidates.size())});
    if (submitCount <= 0) {
        return;
    }

    const auto candidateLess = [](const MeshingCandidate& lhs, const MeshingCandidate& rhs) {
        if (lhs.distanceSq != rhs.distanceSq) {
            return lhs.distanceSq < rhs.distanceSq;
        }
        if (lhs.chunkKey != rhs.chunkKey) {
            return lhs.chunkKey < rhs.chunkKey;
        }
        return lhs.scy < rhs.scy;
    };
    std::partial_sort(candidates.begin(),
                      candidates.begin() + submitCount,
                      candidates.end(),
                      candidateLess);

    const auto submitStartTime = std::chrono::steady_clock::now();
    for (int index = 0; index < submitCount; ++index) {
        const double elapsedMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - submitStartTime).count();
        if (elapsedMs >= static_cast<double>(m_meshingSubmitTimeBudgetMs)) {
            break;
        }

        MeshingCandidate& candidate = candidates[static_cast<size_t>(index)];
        if (candidate.chunk == nullptr) {
            continue;
        }

        SubChunkMeshingJob job;
        job.chunkKey = candidate.chunkKey;
        job.scy = candidate.scy;
        job.revision = candidate.chunk->getSubChunkMeshRevision(candidate.scy);
        job.snapshot = ChunkMesher::captureSubChunkSnapshot(
            *candidate.chunk,
            candidate.scy,
            candidate.neighborPosX.get(),
            candidate.neighborNegX.get(),
            candidate.neighborPosZ.get(),
            candidate.neighborNegZ.get(),
            &worldView);
        if (!job.snapshot) {
            continue;
        }

        const int priority = static_cast<int>(candidate.distanceSq);
        m_meshingService->submit(std::move(job), priority);

        const int64_t flightKey = subChunkFlightKey(candidate.chunkKey, candidate.scy);
        m_meshingInFlight.insert(flightKey);
#ifdef MECRAFT_DEBUG
        ++m_meshingSubmittedThisFrame;
#endif
    }
}

// ---------------------------------------------------------------------------
// Meshing result drain
// ---------------------------------------------------------------------------

void TerrainRenderCache::drainMeshingResults(const IWorldView& worldView) {
    // Phase 1: Drain all completed results from the service into the deferred buffer.
    // This avoids interleaving tryPopCompleted with budget checks, and lets us
    // process results in order with strict vertex/time budgets.
    {
        SubChunkMeshingResult result;
        while (m_meshingService->tryPopCompleted(result)) {
            m_deferredMeshResults.push_back(std::move(result));
        }
    }

    if (m_deferredMeshResults.empty()) {
        return;
    }

    const auto drainStartTime = std::chrono::steady_clock::now();
    int uploadedCount = 0;

    auto subChunkFlightKey = [](int64_t chunkKey, int scy) -> int64_t {
        return (chunkKey & 0x00FFFFFFFFFFFFFFLL) | (static_cast<int64_t>(scy) << 56);
    };

    // Phase 2: Process from deferred buffer respecting budgets.
    // Over-budget results stay in the buffer for the next frame.
    size_t processIdx = 0;
    while (processIdx < m_deferredMeshResults.size() &&
           uploadedCount < m_meshingDrainBudget) {
        const double elapsedMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - drainStartTime).count();
        if (elapsedMs >= m_meshingDrainTimeBudgetMs) {
            break;
        }

        SubChunkMeshingResult& result = m_deferredMeshResults[processIdx];

        // Compute vertex count for budget check BEFORE uploading
        const int currentVertices =
            static_cast<int>(result.meshData.opaqueVertices.size()) +
            static_cast<int>(result.meshData.cutoutVertices.size()) +
            static_cast<int>(result.meshData.cutoutDistanceVertices.size()) +
            static_cast<int>(result.meshData.transparentVertices.size()) +
            static_cast<int>(result.meshData.waterVertices.size());

        // Hard vertex budget: if this result would push us over, allow at most
        // one over-budget upload then stop for this frame.
        const bool overBudget = m_meshUploadVerticesThisFrame + currentVertices > m_meshingDrainVertexBudget;
        if (overBudget && uploadedCount > 0) {
            break;  // Already uploaded something; defer the rest
        }

        // Count result as processed (whether we upload or discard it)
        ++processIdx;
        ++uploadedCount;

#ifdef MECRAFT_DEBUG
        ++m_meshingCompletedThisFrame;
#endif

        const int64_t flightKey = subChunkFlightKey(result.chunkKey, result.scy);
        m_meshingInFlight.erase(flightKey);

        const auto& activeChunks = worldView.getActiveChunks();
        const auto it = activeChunks.find(result.chunkKey);
        if (it == activeChunks.end() || !it->second) {
            continue;
        }

        Chunk& chunk = *it->second;
        if (chunk.getSubChunkMeshRevision(result.scy) != result.revision) {
#ifdef MECRAFT_DEBUG
            ++m_meshingStaleDroppedThisFrame;
#endif
            continue;
        }

#ifdef MECRAFT_DEBUG
        m_meshingBuildMsThisFrame += result.meshData.buildTimeMs;
        m_lastMeshingBuildMs = static_cast<float>(result.meshData.buildTimeMs);
        m_lastOpaqueFacesBeforeGreedy = result.meshData.opaqueFaceCountBeforeGreedy;
        m_lastOpaqueFacesAfterGreedy = result.meshData.opaqueFaceCountAfterGreedy;
        m_lastTransparentFacesBeforeGreedy = result.meshData.transparentFaceCountBeforeGreedy;
        m_lastTransparentFacesAfterGreedy = result.meshData.transparentFaceCountAfterGreedy;
        m_lastOpaqueVertexCount = result.meshData.opaqueVertexCount;
#endif

        // Upload per-sub-chunk mesh and refresh column-level aggregate for opaque/cutout.
        SubChunkMesh mesh;

        const glm::ivec3 worldOff = chunk.getWorldOffset();
        const float txOff = static_cast<float>(worldOff.x);
        const float tyOff = static_cast<float>(worldOff.y);
        const float tzOff = static_cast<float>(worldOff.z);
        const float scyYOff = static_cast<float>(result.scy * SubChunk::SIZE);

        auto bakeWorldOffset = [&](std::vector<BlockVertex>& verts) {
            for (BlockVertex& v : verts) {
                v.x += txOff;
                v.y += tyOff + scyYOff;
                v.z += tzOff;
            }
        };

        if (m_useMultiDrawIndirect) {
            // MDI path: bake world offset and upload to global buffer pool.
            const bool hasOpaqueOrCutout =
                !result.meshData.opaqueVertices.empty() ||
                !result.meshData.cutoutVertices.empty() ||
                !result.meshData.cutoutDistanceVertices.empty();
            std::vector<BlockVertex> opaqueVerts = std::move(result.meshData.opaqueVertices);
            std::vector<BlockVertex> cutoutVerts = std::move(result.meshData.cutoutVertices);
            std::vector<BlockVertex> cutoutDistanceVerts = std::move(result.meshData.cutoutDistanceVertices);
            std::vector<BlockVertex> transparentVerts = std::move(result.meshData.transparentVertices);
            std::vector<BlockVertex> waterVerts = std::move(result.meshData.waterVertices);
            bakeWorldOffset(opaqueVerts);
            bakeWorldOffset(cutoutVerts);
            bakeWorldOffset(cutoutDistanceVerts);
            bakeWorldOffset(transparentVerts);
            bakeWorldOffset(waterVerts);

            const glm::vec3 boundsWorldOffset(txOff, tyOff, tzOff);
            WorldGpuMesh gpu = m_worldRenderBuffer->uploadSubChunk(
                opaqueVerts, cutoutVerts, cutoutDistanceVerts, transparentVerts, waterVerts,
                result.meshData.hasBounds,
                result.meshData.hasBounds ? result.meshData.boundsMin + boundsWorldOffset : glm::vec3(0.0f),
                result.meshData.hasBounds ? result.meshData.boundsMax + boundsWorldOffset : glm::vec3(0.0f));
            if ((!opaqueVerts.empty() && gpu.opaque.vertexCount == 0) ||
                (!cutoutVerts.empty() && gpu.cutout.vertexCount == 0) ||
                (!cutoutDistanceVerts.empty() && gpu.cutoutDistance.vertexCount == 0) ||
                (!transparentVerts.empty() && gpu.transparent.vertexCount == 0) ||
                (!waterVerts.empty() && gpu.water.vertexCount == 0)) {
                continue;
            }

            mesh.opaqueRange = gpu.opaque;
            mesh.cutoutRange = gpu.cutout;
            mesh.cutoutDistanceRange = gpu.cutoutDistance;
            mesh.transparentRange = gpu.transparent;
            mesh.waterRange = gpu.water;
            mesh.opaqueRange.metadataIndex = gpu.metadataIndex;
            mesh.cutoutRange.metadataIndex = gpu.metadataIndex;
            mesh.cutoutDistanceRange.metadataIndex = gpu.metadataIndex;
            mesh.transparentRange.metadataIndex = gpu.metadataIndex;
            mesh.waterRange.metadataIndex = gpu.metadataIndex;
            mesh.vertexCount = gpu.opaque.vertexCount;
            mesh.cutoutVertexCount = gpu.cutout.vertexCount;
            mesh.cutoutDistanceVertexCount = gpu.cutoutDistance.vertexCount;
            mesh.transparentVertexCount = gpu.transparent.vertexCount;
            mesh.waterVertexCount = gpu.water.vertexCount;
            mesh.hasBounds = result.meshData.hasBounds;
            mesh.boundsMin = gpu.boundsMin;
            mesh.boundsMax = gpu.boundsMax;
            mesh.inGlobalPool = true;

            const SubChunkGpuKey gpuKey{result.chunkKey, result.scy};
            releaseMdiAllocation(gpuKey);
            m_mdiMeshAllocations[gpuKey] = MdiMeshAllocation{gpu};
            chunk.setSubChunkMesh(result.scy, mesh);

            // MDI mode only needs column bounds for hierarchical frustum culling.
            chunk.updateColumnAggregateBoundsOnly(result.scy, result.meshData, hasOpaqueOrCutout);
        } else {
            // Old path: per-mesh VAOs.
            mesh.upload(result.meshData.opaqueVertices);
            mesh.uploadCutout(result.meshData.cutoutVertices);
            mesh.uploadCutoutDistance(result.meshData.cutoutDistanceVertices);

            std::vector<BlockVertex> transparentVerts = result.meshData.transparentVertices;
            const uint32_t genericTransparentVertexCount = static_cast<uint32_t>(transparentVerts.size());
            const uint32_t waterVertexCount = static_cast<uint32_t>(result.meshData.waterVertices.size());
            transparentVerts.insert(transparentVerts.end(), result.meshData.waterVertices.begin(), result.meshData.waterVertices.end());
            bakeWorldOffset(transparentVerts);
            mesh.uploadTransparent(transparentVerts);
            mesh.waterVertexCount = waterVertexCount;
            mesh.transparentVertexCount = genericTransparentVertexCount + waterVertexCount;

            mesh.hasBounds = result.meshData.hasBounds;
            mesh.boundsMin = result.meshData.boundsMin;
            mesh.boundsMax = result.meshData.boundsMax;
            chunk.setSubChunkMesh(result.scy, mesh);
            chunk.updateColumnAggregateData(result.scy, result.meshData);
        }

        m_meshUploadVerticesThisFrame += currentVertices;
        m_meshUploadBytesThisFrame += static_cast<size_t>(currentVertices) *
            (m_useMultiDrawIndirect ? sizeof(PackedBlockVertex) : sizeof(BlockVertex));

        if (overBudget) {
            break;  // Allow one over-budget upload, then stop
        }
    }

    // Record upload time
    m_worldBufferUploadMsThisFrame = static_cast<float>(
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - drainStartTime).count());

    // Record pool expand count
    m_worldBufferExpandCountThisFrame = static_cast<int>(
        m_worldRenderBuffer->opaqueExpandCount() +
        m_worldRenderBuffer->cutoutExpandCount() +
        m_worldRenderBuffer->transparentExpandCount());

    // Remove processed results, keep deferred ones
    if (processIdx > 0) {
        m_deferredMeshResults.erase(
            m_deferredMeshResults.begin(),
            m_deferredMeshResults.begin() + static_cast<ptrdiff_t>(processIdx));
    }

    m_meshUploadDeferredCount = static_cast<int>(m_deferredMeshResults.size());
}

// ---------------------------------------------------------------------------
// Transparent batch collection
// ---------------------------------------------------------------------------

void TerrainRenderCache::addTransparentBatch(const GpuMeshRange& range,
                                             const float distanceSq,
                                             const TransparentBatchKind kind) {
    if (range.vertexCount == 0) {
        return;
    }

    m_deferredTransparentBatch.push_back({range, distanceSq, kind});
    if (kind == TransparentBatchKind::Water) {
        ++m_transparentPassPlan.waterCommands;
        m_transparentPassPlan.waterVertices += range.vertexCount;
    } else {
        ++m_transparentPassPlan.genericCommands;
        m_transparentPassPlan.genericVertices += range.vertexCount;
    }
}

void TerrainRenderCache::clearTransparentBatches() {
    m_deferredTransparentBatch.clear();
    m_transparentPassPlan.clear();
}

// ---------------------------------------------------------------------------
// Debug
// ---------------------------------------------------------------------------

void TerrainRenderCache::recordMeshingHistory() {
#ifdef MECRAFT_DEBUG
    m_submittedHistory[m_historyIndex] = m_meshingSubmittedThisFrame;
    m_completedHistory[m_historyIndex] = m_meshingCompletedThisFrame;
    m_inFlightHistory[m_historyIndex] = m_meshingInFlight.size();
    m_historyIndex = (m_historyIndex + 1) % kHistorySize;
#endif
}
