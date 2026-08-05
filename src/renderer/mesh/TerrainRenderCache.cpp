#include "TerrainRenderCache.h"
#include "WorldRenderBuffer.h"
#include "ChunkMesher.h"
#include "../rhi/RhiDevice.h"
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
    int scy = 0; // Sub-chunk index
    bool interactiveLightDirty = false;
    float distanceSq = 0.0f;
    std::shared_ptr<Chunk> chunkRef;
    std::shared_ptr<Chunk> neighborPosX;
    std::shared_ptr<Chunk> neighborNegX;
    std::shared_ptr<Chunk> neighborPosZ;
    std::shared_ptr<Chunk> neighborNegZ;
};

// Expand an AABB to include a candidate bounding box.
void expandBounds(glm::vec3& minBounds, glm::vec3& maxBounds, bool& hasBounds, const glm::vec3& candidateMin,
                  const glm::vec3& candidateMax) {
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

bool TerrainRenderCache::init(RhiDevice* rhiDevice) {
    m_rhiDevice = rhiDevice;
    return m_blasCache.init(rhiDevice);
}

void TerrainRenderCache::beginFrame() {
    ++m_frameSerial;
    collectRetiredMdiAllocations();
    m_blasCache.beginFrame();
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
    if (m_rhiDevice != nullptr && !m_retiredMdiAllocations.empty()) {
        m_rhiDevice->waitIdle();
    }
    if (m_worldRenderBuffer != nullptr) {
        for (const RetiredMdiAllocation& retired : m_retiredMdiAllocations) {
            m_worldRenderBuffer->free(retired.mesh);
        }
    }
    m_retiredMdiAllocations.clear();
    m_blasCache.shutdown();
    m_chunkRenderColumns.clear();
    m_frameSerial = 0;
    m_mdiMeshAllocations.clear();
    m_mdiAllocationSweepInitialized = false;
    m_lastMdiAllocationSweepActiveRevision = 0;
    m_meshingInFlight.clear();
    m_deferredMeshResults.clear();
    m_deferredTransparentBatch.clear();
    m_transparentPassPlan.clear();
    m_lastGraphCompletionToken = {};
    m_rhiDevice = nullptr;
}

void TerrainRenderCache::setMeshingBudgets(const int submitBudget, const int maxInFlight,
                                           const float submitTimeBudgetMs, const int drainBudget,
                                           const float drainTimeBudgetMs, const int drainVertexBudget) {
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
    if (m_chunkRenderColumnsRevision == activeChunkRevision && m_chunkRenderColumnsRegionSize == regionChunkSize) {
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

    bool needsRefresh = !column.stateValid;
    for (int scy = 0; scy < Chunk::NUM_SUB_CHUNKS; ++scy) {
        const uint64_t revision = column.chunk->getSubChunkMeshRevision(scy);
        const SubChunk* sc = column.chunk->getSubChunk(scy);
        const uint64_t fingerprint = sc ? sc->getMesh().metadataFingerprint : 0ULL;
        if (!column.stateValid || column.subChunkMeshRevisions[scy] != revision ||
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

    bool columnHasBounds = false;
    glm::vec3 columnMin(0.0f);
    glm::vec3 columnMax(0.0f);
    column.renderableCount = 0;
    column.transparentCount = 0;
    for (int scy = 0; scy < Chunk::NUM_SUB_CHUNKS; ++scy) {
        const SubChunk* sc = column.chunk->getSubChunk(scy);
        if (!sc) {
            continue;
        }

        const SubChunkMesh& mesh = sc->getMesh();
        if (!mesh.inGlobalPool) {
            continue;
        }
        const bool hasOpaqueOrCutout = mesh.opaqueRange.vertexCount > 0 || mesh.cutoutRange.vertexCount > 0 ||
                                       mesh.cutoutDistanceRange.vertexCount > 0;
        const bool hasTransparent = mesh.transparentRange.vertexCount > 0 || mesh.waterRange.vertexCount > 0;
        if (!hasOpaqueOrCutout && !hasTransparent) {
            continue;
        }

        const int yBase = scy * SubChunk::SIZE;
        const glm::vec3 boundsMin =
            mesh.hasBounds ? mesh.boundsMin : column.worldOffset + glm::vec3(0.0f, static_cast<float>(yBase), 0.0f);
        const glm::vec3 boundsMax =
            mesh.hasBounds ? mesh.boundsMax
                           : column.worldOffset +
                                 glm::vec3(Chunk::SIZE_X, static_cast<float>(yBase + SubChunk::SIZE), Chunk::SIZE_Z);

        ++column.renderableCount;
        expandBounds(columnMin, columnMax, columnHasBounds, boundsMin, boundsMax);

        if (hasTransparent) {
            TransparentSubChunkCache& transparent = column.transparentSubChunks[scy];
            transparent.boundsMin = boundsMin;
            transparent.boundsMax = boundsMax;
            column.transparentScys[column.transparentCount++] = scy;
        }
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

void TerrainRenderCache::releaseMdiAllocationOnly(const SubChunkGpuKey& key) {
    const auto it = m_mdiMeshAllocations.find(key);
    if (it == m_mdiMeshAllocations.end()) {
        return;
    }
    if (m_lastGraphCompletionToken.isValid()) {
        m_retiredMdiAllocations.push_back({it->second.mesh, m_lastGraphCompletionToken});
    } else {
        m_worldRenderBuffer->free(it->second.mesh);
    }
    m_mdiMeshAllocations.erase(it);
}

void TerrainRenderCache::collectRetiredMdiAllocations() {
    if (m_retiredMdiAllocations.empty() || m_worldRenderBuffer == nullptr) {
        return;
    }
    for (auto it = m_retiredMdiAllocations.begin(); it != m_retiredMdiAllocations.end();) {
        bool complete = false;
        if (m_rhiDevice == nullptr || !it->completionToken.isValid() ||
            !m_rhiDevice->isSubmissionComplete(it->completionToken, complete)) {
            ++it;
            continue;
        }
        if (!complete) {
            ++it;
            continue;
        }
        m_worldRenderBuffer->free(it->mesh);
        it = m_retiredMdiAllocations.erase(it);
    }
}

void TerrainRenderCache::releaseMdiAllocation(const SubChunkGpuKey& key) {
    releaseMdiAllocationOnly(key);
    m_blasCache.remove(key);
}

void TerrainRenderCache::releaseStaleMdiAllocations(const IWorldView& worldView) {
    if (m_mdiMeshAllocations.empty()) {
        m_mdiAllocationSweepInitialized = true;
        m_lastMdiAllocationSweepActiveRevision = worldView.getActiveChunkRevision();
        return;
    }

    const uint64_t activeChunkRevision = worldView.getActiveChunkRevision();
    if (m_mdiAllocationSweepInitialized && m_lastMdiAllocationSweepActiveRevision == activeChunkRevision) {
        return;
    }

    const auto& activeChunks = worldView.getActiveChunks();
    for (auto it = m_mdiMeshAllocations.begin(); it != m_mdiMeshAllocations.end();) {
        const auto chunkIt = activeChunks.find(it->first.chunkKey);
        bool release = (chunkIt == activeChunks.end() || !chunkIt->second);
        if (!release) {
            const SubChunk* sc = chunkIt->second->getSubChunk(it->first.scy);
            if (sc == nullptr || !sc->getMesh().inGlobalPool) {
                release = true;
            } else {
                const SubChunkMesh& current = sc->getMesh();
                release = current.opaqueRange.generation != it->second.mesh.opaque.generation ||
                          current.cutoutRange.generation != it->second.mesh.cutout.generation ||
                          current.cutoutDistanceRange.generation != it->second.mesh.cutoutDistance.generation ||
                          current.transparentRange.generation != it->second.mesh.transparent.generation ||
                          current.waterRange.generation != it->second.mesh.water.generation;
            }
        }

        if (release) {
            m_worldRenderBuffer->free(it->second.mesh);
            m_blasCache.remove(it->first);
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
    };

    for (const auto& pair : activeChunks) {
        const int64_t chunkKey = pair.first;
        Chunk& chunk = *pair.second;

        // Check each sub-chunk individually
        for (int scy = 0; scy < Chunk::NUM_SUB_CHUNKS; ++scy) {
            // Skip if not dirty
            if (!chunk.isSubChunkDirty(scy))
                continue;

            // Skip if already in flight
            const int64_t flightKey = subChunkFlightKey(chunkKey, scy);
            if (m_meshingInFlight.find(flightKey) != m_meshingInFlight.end())
                continue;

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
            candidate.interactiveLightDirty = chunk.isInteractiveLightSubChunkDirty(scy);
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
    const int interactiveCandidateCount =
        static_cast<int>(std::count_if(candidates.begin(), candidates.end(), [](const MeshingCandidate& candidate) {
            return candidate.interactiveLightDirty;
        }));
    const int submitBudget = std::max(m_meshingSubmitBudget, interactiveCandidateCount);
    const int submitCount = std::min({submitBudget, availableInFlightSlots, static_cast<int>(candidates.size())});
    if (submitCount <= 0) {
        return;
    }

    const auto candidateLess = [](const MeshingCandidate& lhs, const MeshingCandidate& rhs) {
        if (lhs.interactiveLightDirty != rhs.interactiveLightDirty) {
            return lhs.interactiveLightDirty;
        }
        if (lhs.distanceSq != rhs.distanceSq) {
            return lhs.distanceSq < rhs.distanceSq;
        }
        if (lhs.chunkKey != rhs.chunkKey) {
            return lhs.chunkKey < rhs.chunkKey;
        }
        return lhs.scy < rhs.scy;
    };
    std::partial_sort(candidates.begin(), candidates.begin() + submitCount, candidates.end(), candidateLess);

    const auto submitStartTime = std::chrono::steady_clock::now();
    for (int index = 0; index < submitCount; ++index) {
        const double elapsedMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - submitStartTime).count();
        const MeshingCandidate& candidate = candidates[static_cast<size_t>(index)];
        if (!candidate.interactiveLightDirty && elapsedMs >= static_cast<double>(m_meshingSubmitTimeBudgetMs)) {
            break;
        }

        if (candidate.chunk == nullptr) {
            continue;
        }

        SubChunkMeshingJob job;
        job.chunkKey = candidate.chunkKey;
        job.scy = candidate.scy;
        job.revision = candidate.chunk->getSubChunkMeshRevision(candidate.scy);
        job.snapshot = ChunkMesher::captureSubChunkSnapshot(
            *candidate.chunk, candidate.scy, candidate.neighborPosX.get(), candidate.neighborNegX.get(),
            candidate.neighborPosZ.get(), candidate.neighborNegZ.get(), &worldView);
        if (!job.snapshot) {
            continue;
        }

        const int priority = candidate.interactiveLightDirty ? (-1000000000 + static_cast<int>(candidate.distanceSq))
                                                             : static_cast<int>(candidate.distanceSq);
        m_meshingService->submit(std::move(job), priority);

        const int64_t flightKey = subChunkFlightKey(candidate.chunkKey, candidate.scy);
        m_meshingInFlight.insert(flightKey);
#ifdef MECRAFT_DEBUG
        ++m_meshingSubmittedThisFrame;
#endif
    }
}

bool TerrainRenderCache::isMeshingSettled(const IWorldView& worldView) const {
    if (m_meshingService == nullptr || m_meshingService->inFlightCount() != 0 || !m_meshingInFlight.empty() ||
        !m_deferredMeshResults.empty() || (m_blasCache.supported() && !m_blasCache.isSettled())) {
        return false;
    }

    for (const auto& entry : worldView.getActiveChunks()) {
        if (!entry.second) {
            continue;
        }
        for (int scy = 0; scy < Chunk::NUM_SUB_CHUNKS; ++scy) {
            if (entry.second->isSubChunkDirty(scy)) {
                return false;
            }
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Meshing result drain
// ---------------------------------------------------------------------------

bool TerrainRenderCache::drainMeshingResults(const IWorldView& worldView, RhiCommandList& commandList) {
    // Phase 1: Drain all completed results from the service into the deferred buffer.
    // This avoids interleaving tryPopCompleted with budget checks, and lets us
    // process results in order with strict vertex/time budgets.
    {
        SubChunkMeshingResult result;
        while (m_meshingService->tryPopCompleted(result)) {
            m_deferredMeshResults.push_back(std::move(result));
        }
    }

    const auto& activeChunks = worldView.getActiveChunks();
    if (!m_deferredMeshResults.empty()) {
        std::stable_sort(m_deferredMeshResults.begin(), m_deferredMeshResults.end(),
                         [&](const SubChunkMeshingResult& lhs, const SubChunkMeshingResult& rhs) {
                             const auto lhsIt = activeChunks.find(lhs.chunkKey);
                             const auto rhsIt = activeChunks.find(rhs.chunkKey);
                             const bool lhsInteractive = lhsIt != activeChunks.end() && lhsIt->second &&
                                                         lhsIt->second->isInteractiveLightSubChunkDirty(lhs.scy);
                             const bool rhsInteractive = rhsIt != activeChunks.end() && rhsIt->second &&
                                                         rhsIt->second->isInteractiveLightSubChunkDirty(rhs.scy);
                             return lhsInteractive && !rhsInteractive;
                         });
    }

    bool succeeded = true;
    size_t processIdx = 0u;
    if (!m_deferredMeshResults.empty()) {
        const auto drainStartTime = std::chrono::steady_clock::now();
        int uploadedCount = 0;

        auto subChunkFlightKey = [](int64_t chunkKey, int scy) -> int64_t {
            return (chunkKey & 0x00FFFFFFFFFFFFFFLL) | (static_cast<int64_t>(scy) << 56);
        };

        // Phase 2: Process from deferred buffer respecting budgets.
        // Results beyond the current budgets stay in the buffer for the next frame.
        while (processIdx < m_deferredMeshResults.size()) {
            SubChunkMeshingResult& result = m_deferredMeshResults[processIdx];
            const auto it = activeChunks.find(result.chunkKey);
            const bool interactiveLightMesh =
                it != activeChunks.end() && it->second && it->second->isInteractiveLightSubChunkDirty(result.scy);
            if (!interactiveLightMesh && uploadedCount >= m_meshingDrainBudget) {
                break;
            }

            const double elapsedMs =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - drainStartTime).count();
            if (!interactiveLightMesh && elapsedMs >= m_meshingDrainTimeBudgetMs) {
                break;
            }

            auto recycleResultMeshData = [&]() {
                if (m_meshingService != nullptr) {
                    m_meshingService->recycleMeshData(std::move(result.meshData));
                }
            };

            // Compute vertex count for budget check BEFORE uploading
            const int currentVertices = static_cast<int>(result.meshData.opaqueVertices.size()) +
                                        static_cast<int>(result.meshData.cutoutVertices.size()) +
                                        static_cast<int>(result.meshData.cutoutDistanceVertices.size()) +
                                        static_cast<int>(result.meshData.transparentVertices.size()) +
                                        static_cast<int>(result.meshData.waterVertices.size());

            // Hard vertex budget: if this result would push us over, allow at most
            // one over-budget upload then stop for this frame.
            const bool overBudget = m_meshUploadVerticesThisFrame + currentVertices > m_meshingDrainVertexBudget;
            if (!interactiveLightMesh && overBudget && uploadedCount > 0) {
                break; // Already uploaded something; defer the rest
            }

            // Count result as processed (whether we upload or discard it)
            ++processIdx;
            ++uploadedCount;

#ifdef MECRAFT_DEBUG
            ++m_meshingCompletedThisFrame;
#endif

            const int64_t flightKey = subChunkFlightKey(result.chunkKey, result.scy);
            m_meshingInFlight.erase(flightKey);

            if (it == activeChunks.end() || !it->second) {
                recycleResultMeshData();
                continue;
            }

            Chunk& chunk = *it->second;
            if (chunk.getSubChunkMeshRevision(result.scy) != result.revision) {
#ifdef MECRAFT_DEBUG
                ++m_meshingStaleDroppedThisFrame;
#endif
                recycleResultMeshData();
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

            TerrainBlasGeometry blasGeometry;
            if (m_blasCache.supported()) {
                const TerrainBlasRequestResult preparation =
                    TerrainBlasCache::prepareGeometry(result.meshData.opaqueVertices, result.meshData.cutoutVertices,
                                                      result.meshData.cutoutDistanceVertices, blasGeometry);
                if (preparation == TerrainBlasRequestResult::InvalidGeometry) {
                    recycleResultMeshData();
                    succeeded = false;
                    break;
                }
            }

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

            bakeWorldOffset(result.meshData.opaqueVertices);
            bakeWorldOffset(result.meshData.cutoutVertices);
            bakeWorldOffset(result.meshData.cutoutDistanceVertices);
            bakeWorldOffset(result.meshData.transparentVertices);
            bakeWorldOffset(result.meshData.waterVertices);

            const glm::vec3 boundsWorldOffset(txOff, tyOff + scyYOff, tzOff);
            WorldGpuMesh gpu = m_worldRenderBuffer->uploadSubChunk(
                commandList, result.meshData.opaqueVertices, result.meshData.cutoutVertices,
                result.meshData.cutoutDistanceVertices, result.meshData.transparentVertices,
                result.meshData.waterVertices, result.meshData.hasBounds,
                result.meshData.hasBounds ? result.meshData.boundsMin + boundsWorldOffset : glm::vec3(0.0f),
                result.meshData.hasBounds ? result.meshData.boundsMax + boundsWorldOffset : glm::vec3(0.0f));
            if ((!result.meshData.opaqueVertices.empty() && gpu.opaque.vertexCount == 0) ||
                (!result.meshData.cutoutVertices.empty() && gpu.cutout.vertexCount == 0) ||
                (!result.meshData.cutoutDistanceVertices.empty() && gpu.cutoutDistance.vertexCount == 0) ||
                (!result.meshData.transparentVertices.empty() && gpu.transparent.vertexCount == 0) ||
                (!result.meshData.waterVertices.empty() && gpu.water.vertexCount == 0)) {
                recycleResultMeshData();
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
            mesh.hasBounds = result.meshData.hasBounds;
            mesh.boundsMin = gpu.boundsMin;
            mesh.boundsMax = gpu.boundsMax;
            mesh.inGlobalPool = true;

            const SubChunkGpuKey gpuKey{result.chunkKey, result.scy};
            if (m_blasCache.supported()) {
                const TerrainBlasRequestResult request =
                    m_blasCache.requestBuild(gpuKey, result.revision, boundsWorldOffset, std::move(blasGeometry));
                if (request != TerrainBlasRequestResult::Queued && request != TerrainBlasRequestResult::Cleared &&
                    request != TerrainBlasRequestResult::Unchanged) {
                    m_worldRenderBuffer->free(gpu);
                    recycleResultMeshData();
                    succeeded = false;
                    break;
                }
            }

            releaseMdiAllocationOnly(gpuKey);
            m_mdiMeshAllocations[gpuKey] = MdiMeshAllocation{gpu};
            chunk.setSubChunkMesh(result.scy, mesh);
            recycleResultMeshData();

            m_meshUploadVerticesThisFrame += currentVertices;
            m_meshUploadBytesThisFrame += static_cast<size_t>(currentVertices) * sizeof(PackedBlockVertex);

            if (!interactiveLightMesh && overBudget) {
                break; // Allow one over-budget upload, then stop
            }
        }

        // Record upload time
        m_worldBufferUploadMsThisFrame = static_cast<float>(
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - drainStartTime).count());

        // Record pool expand count
        m_worldBufferExpandCountThisFrame =
            static_cast<int>(m_worldRenderBuffer->opaqueExpandCount() + m_worldRenderBuffer->cutoutExpandCount() +
                             m_worldRenderBuffer->transparentExpandCount());
    }

    // Remove processed results, keep deferred ones
    if (processIdx > 0) {
        m_deferredMeshResults.erase(m_deferredMeshResults.begin(),
                                    m_deferredMeshResults.begin() + static_cast<ptrdiff_t>(processIdx));
    }

    m_meshUploadDeferredCount = static_cast<int>(m_deferredMeshResults.size());
    if (succeeded && !m_blasCache.recordFrame(commandList)) {
        succeeded = false;
    }
    return succeeded;
}

void TerrainRenderCache::finishGraphExecution(const bool succeeded, const RhiSubmissionToken completionToken) {
    if (completionToken.isValid()) {
        m_lastGraphCompletionToken = completionToken;
    }
    m_blasCache.finishGraphExecution(succeeded, completionToken);
}

// ---------------------------------------------------------------------------
// Transparent batch collection
// ---------------------------------------------------------------------------

void TerrainRenderCache::addTransparentBatch(const GpuMeshRange& range, const float distanceSq,
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
