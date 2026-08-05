#include "LightService.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <utility>

#include "../chunk/Chunk.h"
#include "LightSolver.h"
#include "../../thread/ThreadPool.h"
#include "../World.h"

namespace {
std::shared_ptr<const Chunk> findSharedByCoords(const World& world, const int cx, const int cz) {
    const int64_t key = World::chunkKey(cx, cz);
    auto it = world.getActiveChunks().find(key);
    if (it == world.getActiveChunks().end() || !it->second) {
        return nullptr;
    }

    return std::static_pointer_cast<const Chunk>(it->second);
}

int reasonBias(const LightDirtyReason reason) {
    switch (reason) {
    case LightDirtyReason::BlockChanged: return 0;
    case LightDirtyReason::ChunkLoaded: return 500;
    case LightDirtyReason::NeighborBoundary:
    default: return 1000;
    }
}

int dirtyReasonPriority(const LightDirtyReason reason) {
    switch (reason) {
    case LightDirtyReason::BlockChanged: return 2;
    case LightDirtyReason::ChunkLoaded: return 1;
    case LightDirtyReason::NeighborBoundary:
    default: return 0;
    }
}

bool shouldReplaceDirtyReason(const LightDirtyReason current, const LightDirtyReason incoming) {
    return dirtyReasonPriority(incoming) > dirtyReasonPriority(current);
}

bool sameBoundaryNodes(const BorderUpdateBatch& lhs, const BorderUpdateBatch& rhs) {
    if (lhs.nodes.size() != rhs.nodes.size()) {
        return false;
    }

    for (std::size_t i = 0; i < lhs.nodes.size(); ++i) {
        const BorderLightNode& a = lhs.nodes[i];
        const BorderLightNode& b = rhs.nodes[i];
        if (a.localX != b.localX || a.y != b.y || a.localZ != b.localZ || a.level != b.level || a.kind != b.kind) {
            return false;
        }
    }
    return true;
}

uint8_t directionBit(const int direction) {
    return static_cast<uint8_t>(1u << direction);
}

int oppositeDirection(const int direction) {
    switch (direction) {
    case 0: return 1;
    case 1: return 0;
    case 2: return 3;
    case 3: return 2;
    default: return 0;
    }
}

uint32_t expandSubChunkMaskVertical(uint32_t mask) {
    uint32_t expanded = mask;
    for (int scy = 0; scy < Chunk::NUM_SUB_CHUNKS; ++scy) {
        if ((mask & (1u << scy)) == 0u) {
            continue;
        }
        if (scy > 0) {
            expanded |= (1u << (scy - 1));
        }
        if (scy + 1 < Chunk::NUM_SUB_CHUNKS) {
            expanded |= (1u << (scy + 1));
        }
    }
    return expanded;
}

void markSubChunksDirtyByMask(Chunk& chunk, uint32_t mask) {
    mask = expandSubChunkMaskVertical(mask);
    for (int scy = 0; scy < Chunk::NUM_SUB_CHUNKS; ++scy) {
        if ((mask & (1u << scy)) != 0u) {
            chunk.markSubChunkDirty(scy);
        }
    }
}

void countReason(const LightDirtyReason reason, int& chunkLoaded, int& blockChanged, int& neighborBoundary) {
    switch (reason) {
    case LightDirtyReason::ChunkLoaded: ++chunkLoaded; break;
    case LightDirtyReason::BlockChanged: ++blockChanged; break;
    case LightDirtyReason::NeighborBoundary:
    default: ++neighborBoundary; break;
    }
}
} // namespace

LightService::LightService(World& world) : m_world(world) {}

LightService::~LightService() = default;

bool LightService::isInteractiveReason(const LightDirtyReason reason) {
    return reason == LightDirtyReason::BlockChanged || reason == LightDirtyReason::NeighborBoundary;
}

void LightService::start(ThreadPool* pool) {
    m_pool = pool;
    m_running = true;
}

void LightService::shutdown() {
    m_running = false;
    m_pool = nullptr;
    m_frameStats = {};
    m_frameBlockChangedChunks.clear();
    m_completedCount.store(0);

    std::lock_guard<std::mutex> stateLock(m_stateMutex);
    m_chunkStates.clear();

    std::lock_guard<std::mutex> completedLock(m_completedMutex);
    m_completed = {};
}

void LightService::onChunkLoaded(const std::shared_ptr<Chunk>& chunk) {
    if (!m_running || m_pool == nullptr || !chunk) {
        return;
    }

    const int64_t key = World::chunkKey(chunk->m_chunkX, chunk->m_chunkZ);
    markChunkDirty(key, LightDirtyReason::ChunkLoaded);
    ensureBaseLightCache(chunk);

    // New chunk and loaded neighbors both need one boundary synchronization pass.
    uint8_t newChunkForceOutgoingMask = 0;
    for (int direction = 0; direction < 4; ++direction) {
        Chunk* neighbor = chunk->neighbors[direction];
        if (!neighbor) {
            continue;
        }
        newChunkForceOutgoingMask |= directionBit(direction);
        const int64_t neighborKey = World::chunkKey(neighbor->m_chunkX, neighbor->m_chunkZ);
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            LightChunkState& state = m_chunkStates[neighborKey];
            state.pendingForceOutgoingBoundaryMask |= directionBit(oppositeDirection(direction));
        }
        markChunkDirty(neighborKey, LightDirtyReason::NeighborBoundary);
    }
    if (newChunkForceOutgoingMask != 0u) {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        LightChunkState& state = m_chunkStates[key];
        state.pendingForceOutgoingBoundaryMask |= newChunkForceOutgoingMask;
    }
}

void LightService::onChunkUnloaded(const int64_t chunkKey) {
    std::lock_guard<std::mutex> lock(m_stateMutex);

    auto it = m_world.getActiveChunks().find(chunkKey);
    if (it != m_world.getActiveChunks().end() && it->second) {
        Chunk& chunk = *it->second;
        chunk.setLightQueued(false);
        chunk.setLightInFlight(false);

        for (int direction = 0; direction < 4; ++direction) {
            Chunk* neighbor = chunk.neighbors[direction];
            if (!neighbor) {
                continue;
            }

            const int64_t neighborKey = World::chunkKey(neighbor->m_chunkX, neighbor->m_chunkZ);
            LightChunkState& neighborState = m_chunkStates[neighborKey];
            const bool wasDirty = neighborState.dirty;
            if (!neighborState.pendingBoundaryChanged[direction]) {
                neighborState.pendingPreviousBoundaryCache[direction] = neighborState.boundaryCache[direction];
            }
            neighborState.boundaryCache[direction].reset();
            neighborState.pendingBoundaryChanged[direction] = true;
            neighbor->bumpLightRevision();
            neighborState.dirty = true;
            if (!wasDirty || shouldReplaceDirtyReason(neighborState.reason, LightDirtyReason::NeighborBoundary)) {
                neighborState.reason = LightDirtyReason::NeighborBoundary;
            }

            if (!neighborState.inFlight && !neighborState.queued) {
                neighborState.queued = true;
                neighbor->setLightQueued(true);
            }
        }
    }

    m_chunkStates.erase(chunkKey);
    invalidateBaseLightCache(chunkKey);
}

void LightService::onBlockChanged(const int wx, const int wy, const int wz, const BlockStateId oldStateId,
                                  const BlockStateId newStateId) {
    if (!m_running || m_pool == nullptr) {
        return;
    }

    const BlockID oldId = BlockStateRegistry::getBlockId(oldStateId);
    const BlockID newId = BlockStateRegistry::getBlockId(newStateId);
    const glm::ivec2 chunkCoords = m_world.getChunkCoords(wx, wz);
    const int64_t key = World::chunkKey(chunkCoords.x, chunkCoords.y);
    ++m_frameStats.blockChangeCalls;
    m_frameStats.lastBlockChangeX = wx;
    m_frameStats.lastBlockChangeY = wy;
    m_frameStats.lastBlockChangeZ = wz;
    m_frameStats.lastBlockChangeOld = oldId;
    m_frameStats.lastBlockChangeNew = newId;
    m_frameBlockChangedChunks.insert(key);
    m_frameStats.blockChangeUniqueChunks = static_cast<int>(m_frameBlockChangedChunks.size());

    auto it = m_world.getActiveChunks().find(key);
    if (it != m_world.getActiveChunks().end() && it->second) {
        const int localX = wx - chunkCoords.x * Chunk::SIZE_X;
        const int localZ = wz - chunkCoords.y * Chunk::SIZE_Z;
        const uint8_t oldEmission =
            BlockRegistry::isLightSourceFast(oldId) ? BlockRegistry::getLightLevelFast(oldId) : 0;
        const uint8_t newEmission =
            BlockRegistry::isLightSourceFast(newId) ? BlockRegistry::getLightLevelFast(newId) : 0;
        const bool emissionDecreased = newEmission < oldEmission;
        it->second->recalcHeightMap(localX, localZ);
        it->second->bumpLightRevision();
        updateBaseLightCacheForBlockChange(*it->second, localX, wy, localZ, oldId, newId);

        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            LightChunkState& state = m_chunkStates[key];
            state.pendingBlockChanges.push_back(
                {static_cast<uint8_t>(localX), static_cast<uint8_t>(wy), static_cast<uint8_t>(localZ), oldId, newId});

            // Boundary batches contain propagated light, not provenance. When
            // block-light emission decreases, previously received neighbor input
            // may have originated from this source. Suppress those inputs for the
            // edit job and accept refreshed state only after this result applies.
            if (emissionDecreased) {
                for (int direction = 0; direction < 4; ++direction) {
                    if (!state.pendingBoundaryChanged[direction]) {
                        state.pendingPreviousBoundaryCache[direction] = state.boundaryCache[direction];
                    }
                    state.boundaryCache[direction].reset();
                    state.pendingBoundaryChanged[direction] = true;
                }
                state.boundaryInvalidationMask = 0x0Fu;
                state.pendingSuppressedBoundaryMask = 0x0Fu;
            }
        }
    }
    markChunkDirty(key, LightDirtyReason::BlockChanged);
}

void LightService::submitJobs(const glm::vec3& cameraPos, const int submitBudget) {
    m_frameStats.submitted = 0;
    m_frameStats.completed = 0;
    m_frameStats.nodesVisited = 0;
    m_frameStats.workerMs = 0.0f;
    m_frameStats.mergeMs = 0.0f;
    m_frameStats.staleDropped = 0;
    m_frameStats.requeued = 0;
    m_frameStats.submittedChunkLoaded = 0;
    m_frameStats.submittedBlockChanged = 0;
    m_frameStats.submittedNeighborBoundary = 0;
    m_frameStats.captureMs = 0.0f;
    m_frameStats.captureCount = 0;

    if (!m_running || m_pool == nullptr || submitBudget <= 0) {
        return;
    }

    // Backpressure: if the completed queue is backing up, skip submission
    // this frame and let drainCompleted catch up first.
    if (m_completedCount.load() > 64) {
        return;
    }

    int submitted = 0;
    while (submitted < submitBudget) {
        int64_t chunkKey = 0;
        LightJob job;
        LightDirtyReason pickedReason = LightDirtyReason::NeighborBoundary;
        bool found = false;

        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            float bestPriority = std::numeric_limits<float>::max();
            auto bestIt = m_chunkStates.end();

            for (auto it = m_chunkStates.begin(); it != m_chunkStates.end(); ++it) {
                LightChunkState& state = it->second;
                if (!state.queued || state.inFlight || !state.dirty) {
                    continue;
                }

                auto chunkIt = m_world.getActiveChunks().find(it->first);
                if (chunkIt == m_world.getActiveChunks().end() || !chunkIt->second) {
                    continue;
                }

                const float centerX = static_cast<float>(chunkIt->second->m_chunkX * Chunk::SIZE_X + Chunk::SIZE_X / 2);
                const float centerZ = static_cast<float>(chunkIt->second->m_chunkZ * Chunk::SIZE_Z + Chunk::SIZE_Z / 2);
                const float dx = centerX - cameraPos.x;
                const float dz = centerZ - cameraPos.z;
                const float distanceSq = dx * dx + dz * dz;
                const float priority = distanceSq + static_cast<float>(reasonBias(state.reason));
                if (priority < bestPriority) {
                    bestPriority = priority;
                    bestIt = it;
                }
            }

            if (bestIt != m_chunkStates.end()) {
                chunkKey = bestIt->first;
                LightChunkState& state = bestIt->second;
                auto chunkIt = m_world.getActiveChunks().find(chunkKey);
                if (chunkIt != m_world.getActiveChunks().end() && chunkIt->second) {
                    pickedReason = state.reason;
                    state.lastSubmitReason = pickedReason;
                    state.inFlight = true;
                    state.queued = false;
                    state.dirty = false;

                    job.chunkKey = chunkKey;
                    job.revision = chunkIt->second->getLightRevision();
                    job.reason = pickedReason;
                    job.chunk = chunkIt->second;
                    job.neighborPosX =
                        findSharedByCoords(m_world, chunkIt->second->m_chunkX + 1, chunkIt->second->m_chunkZ);
                    job.neighborNegX =
                        findSharedByCoords(m_world, chunkIt->second->m_chunkX - 1, chunkIt->second->m_chunkZ);
                    job.neighborPosZ =
                        findSharedByCoords(m_world, chunkIt->second->m_chunkX, chunkIt->second->m_chunkZ + 1);
                    job.neighborNegZ =
                        findSharedByCoords(m_world, chunkIt->second->m_chunkX, chunkIt->second->m_chunkZ - 1);
                    const auto captureStart = std::chrono::steady_clock::now();
                    job.blockSnapshot = captureBlockSnapshot(*chunkIt->second);
                    job.blockChanges = state.pendingBlockChanges;
                    job.previousInbox = collectBoundaryInputs(state.pendingPreviousBoundaryCache);
                    job.changedBoundaryDirections = state.pendingBoundaryChanged;
                    job.inbox = collectBoundaryInputs(state);
                    job.packedLightSnapshot = capturePackedLightSnapshot(*chunkIt->second);
                    job.suppressedBoundaryMask = state.pendingSuppressedBoundaryMask;
                    job.forceOutgoingBoundaryMask = state.pendingForceOutgoingBoundaryMask;
                    state.inFlightHaloMeshDirtyMask = state.pendingHaloMeshDirtyMask;
                    state.inFlightSuppressedBoundaryMask = state.pendingSuppressedBoundaryMask;
                    state.inFlightForceOutgoingBoundaryMask = state.pendingForceOutgoingBoundaryMask;
                    state.pendingHaloMeshDirtyMask = 0;
                    state.pendingSuppressedBoundaryMask = 0;
                    state.pendingForceOutgoingBoundaryMask = 0;

                    // Snapshot the incrementally-maintained base-light cache so
                    // the solver can skip the expensive buildCurrentBasePacked() scan.
                    auto cacheIt = m_baseLightCaches.find(chunkKey);
                    if (cacheIt != m_baseLightCaches.end()) {
                        job.baseLightPacked = cacheIt->second.packed;
                    }
                    m_frameStats.captureMs += static_cast<float>(
                        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - captureStart)
                            .count());
                    ++m_frameStats.captureCount;

                    state.inFlightRevision = job.revision;
                    chunkIt->second->setLightQueued(false);
                    chunkIt->second->setLightInFlight(true);
                    found = true;
                }
            }
        }

        if (!found) {
            break;
        }

        ++submitted;
        ++m_frameStats.submitted;
        countReason(pickedReason, m_frameStats.submittedChunkLoaded, m_frameStats.submittedBlockChanged,
                    m_frameStats.submittedNeighborBoundary);

        m_pool->submit(
            [this, job = std::move(job)]() {
                CompletedTicket ticket;
                ticket.result = LightSolver::solve(job);
                onWorkerCompleted(std::move(ticket));
            },
            reasonBias(pickedReason));
    }

    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        int inFlight = 0;
        for (const auto& entry : m_chunkStates) {
            if (entry.second.inFlight) {
                ++inFlight;
            }
        }
        m_frameStats.inFlight = inFlight;
    }
}

void LightService::drainCompleted(World& world, const int mergeBudget, const float timeBudgetMs) {
    if (!m_running) {
        return;
    }

    if (mergeBudget <= 0) {
        return;
    }

    auto mergeStart = std::chrono::steady_clock::now();
    int merged = 0;
    while (merged < mergeBudget) {
        const float elapsedMs = static_cast<float>(
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - mergeStart).count());
        if (elapsedMs >= timeBudgetMs) {
            break;
        }

        CompletedTicket ticket;
        {
            std::lock_guard<std::mutex> lock(m_completedMutex);
            if (m_completed.empty()) {
                break;
            }
            ticket = std::move(m_completed.front());
            m_completed.pop_front();
        }

        m_completedCount.fetch_sub(1);

        ++merged;
        ++m_frameStats.completed;
        m_frameStats.workerMs += ticket.result.workerMs;
        m_frameStats.nodesVisited += static_cast<int>(ticket.result.nodesVisited);

        bool shouldRequeue = false;
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            auto it = m_chunkStates.find(ticket.result.selfDelta.chunkKey);
            if (it == m_chunkStates.end()) {
                ++m_frameStats.staleDropped;
                continue;
            }

            LightChunkState& state = it->second;
            auto chunkIt = world.getActiveChunks().find(ticket.result.selfDelta.chunkKey);
            if (chunkIt != world.getActiveChunks().end() && chunkIt->second) {
                chunkIt->second->setLightInFlight(false);
            }
            if (chunkIt == world.getActiveChunks().end() || !chunkIt->second ||
                chunkIt->second->getLightRevision() != ticket.result.selfDelta.revision ||
                state.inFlightRevision != ticket.result.selfDelta.revision) {
                ++m_frameStats.staleDropped;
                state.inFlight = false;
                state.queued = false;
                shouldRequeue = state.dirty;
                if (shouldRequeue) {
                    state.pendingHaloMeshDirtyMask |= state.inFlightHaloMeshDirtyMask;
                    state.pendingSuppressedBoundaryMask |= state.inFlightSuppressedBoundaryMask;
                    state.pendingForceOutgoingBoundaryMask |= state.inFlightForceOutgoingBoundaryMask;
                    state.queued = true;
                    ++m_frameStats.requeued;
                    // If the dropped job was ChunkLoaded, preserve the reason so
                    // the re-queued job triggers a full rebuild instead of an
                    // incremental solve (which would miss sources that were never
                    // seeded because packedLightSnapshot may be uninitialized).
                    if (state.lastSubmitReason == LightDirtyReason::ChunkLoaded) {
                        state.reason = LightDirtyReason::ChunkLoaded;
                    }
                    if (chunkIt != world.getActiveChunks().end() && chunkIt->second) {
                        chunkIt->second->setLightQueued(true);
                    }
                }
                state.inFlightHaloMeshDirtyMask = 0;
                state.inFlightSuppressedBoundaryMask = 0;
                state.inFlightForceOutgoingBoundaryMask = 0;
                continue;
            }

            const uint32_t haloMeshDirtyMask = state.inFlightHaloMeshDirtyMask;
            const uint8_t suppressedBoundaryMask = state.inFlightSuppressedBoundaryMask;
            if (!ticket.result.selfDelta.packedLight.empty()) {
                uint32_t appliedDirtyMask = 0;
                chunkIt->second->replacePackedLight(ticket.result.selfDelta.packedLight.data(),
                                                    ticket.result.selfDelta.packedLight.size(), &appliedDirtyMask);
                if (appliedDirtyMask != 0u && m_lightChangeCallback) {
                    m_lightChangeCallback(ticket.result.selfDelta.chunkKey, appliedDirtyMask);
                }
                if (haloMeshDirtyMask != 0u) {
                    markSubChunksDirtyByMask(*chunkIt->second, haloMeshDirtyMask);
                }
            }
            state.inFlightHaloMeshDirtyMask = 0;
            state.inFlightSuppressedBoundaryMask = 0;
            state.inFlightForceOutgoingBoundaryMask = 0;
            state.boundaryInvalidationMask &= static_cast<uint8_t>(~suppressedBoundaryMask);
            state.boundaryInvalidationMask |= state.pendingSuppressedBoundaryMask;

            if (suppressedBoundaryMask != 0u) {
                for (int direction = 0; direction < 4; ++direction) {
                    if ((suppressedBoundaryMask & directionBit(direction)) == 0u) {
                        continue;
                    }
                    Chunk* neighbor = chunkIt->second->neighbors[direction];
                    if (!neighbor) {
                        continue;
                    }
                    const int64_t neighborKey = World::chunkKey(neighbor->m_chunkX, neighbor->m_chunkZ);
                    LightChunkState& neighborState = m_chunkStates[neighborKey];
                    neighborState.pendingForceOutgoingBoundaryMask |= directionBit(oppositeDirection(direction));
                    const bool wasDirty = neighborState.dirty;
                    neighborState.dirty = true;
                    if (!wasDirty || shouldReplaceDirtyReason(neighborState.reason, LightDirtyReason::NeighborBoundary)) {
                        neighborState.reason = LightDirtyReason::NeighborBoundary;
                    }
                    if (!neighborState.inFlight && !neighborState.queued) {
                        neighborState.queued = true;
                        neighbor->setLightQueued(true);
                    }
                }
            }

            for (const BorderUpdateBatch& batch : ticket.result.outgoing) {
                auto neighborChunkIt = world.getActiveChunks().find(batch.targetChunkKey);
                if (neighborChunkIt == world.getActiveChunks().end() || !neighborChunkIt->second) {
                    continue;
                }

                LightChunkState& neighborState = m_chunkStates[batch.targetChunkKey];
                bool boundaryChanged = true;
                if (batch.fromDirection < neighborState.boundaryCache.size()) {
                    if ((neighborState.boundaryInvalidationMask & directionBit(batch.fromDirection)) != 0u) {
                        ++m_frameStats.boundarySync;
                        continue;
                    }

                    const std::optional<BorderUpdateBatch>& previous = neighborState.boundaryCache[batch.fromDirection];
                    boundaryChanged = !previous.has_value() || !sameBoundaryNodes(*previous, batch);
                    if (boundaryChanged) {
                        // Preserve the first pre-change batch until the coalesced job is captured.
                        if (!neighborState.pendingBoundaryChanged[batch.fromDirection]) {
                            neighborState.pendingPreviousBoundaryCache[batch.fromDirection] = previous;
                        }
                        neighborState.boundaryCache[batch.fromDirection] = batch;
                        neighborState.pendingBoundaryChanged[batch.fromDirection] = true;
                        neighborChunkIt->second->bumpLightRevision();
                    }
                }
                if (boundaryChanged) {
                    if (batch.dirtySubChunkMask != 0u) {
                        neighborState.pendingHaloMeshDirtyMask |= batch.dirtySubChunkMask;
                    }
                    const bool wasDirty = neighborState.dirty;
                    neighborState.dirty = true;
                    if (!wasDirty ||
                        shouldReplaceDirtyReason(neighborState.reason, LightDirtyReason::NeighborBoundary)) {
                        neighborState.reason = LightDirtyReason::NeighborBoundary;
                    }
                    if (!neighborState.inFlight && !neighborState.queued) {
                        neighborState.queued = true;
                        neighborChunkIt->second->setLightQueued(true);
                    }
                } else if (batch.dirtySubChunkMask != 0u) {
                    markSubChunksDirtyByMask(*neighborChunkIt->second, batch.dirtySubChunkMask);
                }
                ++m_frameStats.boundarySync;
            }

            // Clear inputs that were consumed by the successfully applied job.
            // New inputs added between submission and drain (from concurrent
            // onBlockChanged / boundary updates) are preserved because those
            // events also bump dirty/reason, ensuring the chunk is re-queued.
            state.pendingBlockChanges.clear();
            clearBoundaryInputs(state.pendingPreviousBoundaryCache);
            state.pendingBoundaryChanged.fill(false);

            state.inFlight = false;
            state.queued = false;
            shouldRequeue = state.dirty;
            if (shouldRequeue) {
                state.queued = true;
                ++m_frameStats.requeued;
                chunkIt->second->setLightQueued(true);
            } else {
                state.pendingHaloMeshDirtyMask = 0;
            }
        }
    }

    m_frameStats.mergeMs = static_cast<float>(
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - mergeStart).count());

    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        int inFlight = 0;
        for (const auto& entry : m_chunkStates) {
            if (entry.second.inFlight) {
                ++inFlight;
            }
        }
        m_frameStats.inFlight = inFlight;
    }
}

int LightService::processInteractiveJobsInline(const glm::vec3& cameraPos, const int jobBudget, const int mergeBudget,
                                               const float mergeTimeBudgetMs) {
    if (!m_running || m_pool == nullptr || jobBudget <= 0) {
        return 0;
    }

    int solved = 0;
    while (solved < jobBudget) {
        int64_t chunkKey = 0;
        LightJob job;
        LightDirtyReason pickedReason = LightDirtyReason::NeighborBoundary;
        bool found = false;

        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            float bestPriority = std::numeric_limits<float>::max();
            auto bestIt = m_chunkStates.end();

            for (auto it = m_chunkStates.begin(); it != m_chunkStates.end(); ++it) {
                LightChunkState& state = it->second;
                if (!state.queued || state.inFlight || !state.dirty || !isInteractiveReason(state.reason)) {
                    continue;
                }

                auto chunkIt = m_world.getActiveChunks().find(it->first);
                if (chunkIt == m_world.getActiveChunks().end() || !chunkIt->second) {
                    continue;
                }

                const float centerX = static_cast<float>(chunkIt->second->m_chunkX * Chunk::SIZE_X + Chunk::SIZE_X / 2);
                const float centerZ = static_cast<float>(chunkIt->second->m_chunkZ * Chunk::SIZE_Z + Chunk::SIZE_Z / 2);
                const float dx = centerX - cameraPos.x;
                const float dz = centerZ - cameraPos.z;
                const float distanceSq = dx * dx + dz * dz;
                const float priority = distanceSq + static_cast<float>(reasonBias(state.reason));
                if (priority < bestPriority) {
                    bestPriority = priority;
                    bestIt = it;
                }
            }

            if (bestIt != m_chunkStates.end()) {
                chunkKey = bestIt->first;
                LightChunkState& state = bestIt->second;
                auto chunkIt = m_world.getActiveChunks().find(chunkKey);
                if (chunkIt != m_world.getActiveChunks().end() && chunkIt->second) {
                    pickedReason = state.reason;
                    state.lastSubmitReason = pickedReason;
                    state.inFlight = true;
                    state.queued = false;
                    state.dirty = false;

                    job.chunkKey = chunkKey;
                    job.revision = chunkIt->second->getLightRevision();
                    job.reason = pickedReason;
                    job.chunk = chunkIt->second;
                    job.neighborPosX =
                        findSharedByCoords(m_world, chunkIt->second->m_chunkX + 1, chunkIt->second->m_chunkZ);
                    job.neighborNegX =
                        findSharedByCoords(m_world, chunkIt->second->m_chunkX - 1, chunkIt->second->m_chunkZ);
                    job.neighborPosZ =
                        findSharedByCoords(m_world, chunkIt->second->m_chunkX, chunkIt->second->m_chunkZ + 1);
                    job.neighborNegZ =
                        findSharedByCoords(m_world, chunkIt->second->m_chunkX, chunkIt->second->m_chunkZ - 1);
                    const auto captureStart = std::chrono::steady_clock::now();
                    job.blockSnapshot = captureBlockSnapshot(*chunkIt->second);
                    job.blockChanges = state.pendingBlockChanges;
                    job.previousInbox = collectBoundaryInputs(state.pendingPreviousBoundaryCache);
                    job.changedBoundaryDirections = state.pendingBoundaryChanged;
                    job.inbox = collectBoundaryInputs(state);
                    job.packedLightSnapshot = capturePackedLightSnapshot(*chunkIt->second);
                    job.suppressedBoundaryMask = state.pendingSuppressedBoundaryMask;
                    job.forceOutgoingBoundaryMask = state.pendingForceOutgoingBoundaryMask;
                    state.inFlightHaloMeshDirtyMask = state.pendingHaloMeshDirtyMask;
                    state.inFlightSuppressedBoundaryMask = state.pendingSuppressedBoundaryMask;
                    state.inFlightForceOutgoingBoundaryMask = state.pendingForceOutgoingBoundaryMask;
                    state.pendingHaloMeshDirtyMask = 0;
                    state.pendingSuppressedBoundaryMask = 0;
                    state.pendingForceOutgoingBoundaryMask = 0;

                    auto cacheIt = m_baseLightCaches.find(chunkKey);
                    if (cacheIt != m_baseLightCaches.end()) {
                        job.baseLightPacked = cacheIt->second.packed;
                    }
                    m_frameStats.captureMs += static_cast<float>(
                        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - captureStart)
                            .count());
                    ++m_frameStats.captureCount;

                    state.inFlightRevision = job.revision;
                    chunkIt->second->setLightQueued(false);
                    chunkIt->second->setLightInFlight(true);
                    found = true;
                }
            }
        }

        if (!found) {
            break;
        }

        ++solved;
        ++m_frameStats.submitted;
        countReason(pickedReason, m_frameStats.submittedChunkLoaded, m_frameStats.submittedBlockChanged,
                    m_frameStats.submittedNeighborBoundary);

        CompletedTicket ticket;
        ticket.result = LightSolver::solve(job);
        {
            std::lock_guard<std::mutex> lock(m_completedMutex);
            m_completed.push_front(std::move(ticket));
        }
        m_completedCount.fetch_add(1);

        drainCompleted(m_world, mergeBudget, mergeTimeBudgetMs);
    }

    return solved;
}

LightFrameStats LightService::getFrameStats() const {
    LightFrameStats stats = m_frameStats;
    stats.pendingCompleted = m_completedCount.load();

    std::lock_guard<std::mutex> lock(m_stateMutex);
    stats.inFlight = 0;
    stats.queued = 0;
    stats.dirty = 0;
    stats.dirtyChunkLoaded = 0;
    stats.dirtyBlockChanged = 0;
    stats.dirtyNeighborBoundary = 0;
    for (const auto& entry : m_chunkStates) {
        if (entry.second.inFlight) {
            ++stats.inFlight;
        }
        if (entry.second.queued) {
            ++stats.queued;
        }
        if (entry.second.dirty) {
            ++stats.dirty;
            countReason(entry.second.reason, stats.dirtyChunkLoaded, stats.dirtyBlockChanged,
                        stats.dirtyNeighborBoundary);
        }
    }
    return stats;
}

void LightService::markChunkDirty(const int64_t chunkKey, const LightDirtyReason reason) {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    LightChunkState& state = m_chunkStates[chunkKey];
    const bool wasDirty = state.dirty;
    state.dirty = true;

    if (!wasDirty || shouldReplaceDirtyReason(state.reason, reason)) {
        state.reason = reason;
    }

    if (!state.inFlight && !state.queued) {
        state.queued = true;
        auto it = m_world.getActiveChunks().find(chunkKey);
        if (it != m_world.getActiveChunks().end() && it->second) {
            it->second->setLightQueued(true);
        }
    }
}

void LightService::onWorkerCompleted(CompletedTicket ticket) {
    std::lock_guard<std::mutex> lock(m_completedMutex);
    m_completed.push_back(std::move(ticket));
    m_completedCount.fetch_add(1);
}

int LightService::countDirtyChunks() const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    int count = 0;
    for (const auto& entry : m_chunkStates) {
        if (entry.second.dirty) {
            ++count;
        }
    }
    return count;
}

int LightService::countPendingInteractiveJobs() const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    int count = 0;
    for (const auto& entry : m_chunkStates) {
        const LightChunkState& state = entry.second;
        if (state.dirty && isInteractiveReason(state.reason)) {
            ++count;
        }
    }
    return count;
}

void LightService::ensureBaseLightCache(const std::shared_ptr<Chunk>& chunk) {
    if (!chunk) {
        return;
    }
    const int64_t key = World::chunkKey(chunk->m_chunkX, chunk->m_chunkZ);
    if (m_baseLightCaches.find(key) != m_baseLightCaches.end()) {
        return;
    }
    m_baseLightCaches[key] = buildBaseLightFromChunk(*chunk);
}

void LightService::invalidateBaseLightCache(const int64_t chunkKey) {
    m_baseLightCaches.erase(chunkKey);
}

void LightService::updateBaseLightCacheForBlockChange(const Chunk& chunk, const int localX, const int y,
                                                      const int localZ, const BlockID oldId, const BlockID newId) {
    const int64_t key = World::chunkKey(chunk.m_chunkX, chunk.m_chunkZ);
    auto it = m_baseLightCaches.find(key);
    if (it == m_baseLightCaches.end()) {
        return;
    }

    CachedBaseLight& cache = it->second;
    const uint8_t oldOpacity = BlockRegistry::getOpacityFast(oldId);
    const uint8_t newOpacity = BlockRegistry::getOpacityFast(newId);

    // Sky light: recompute the column if opacity changed.
    if (oldOpacity != newOpacity) {
        recomputeSkyColumn(chunk, localX, localZ, cache.packed);
    }

    // Block light: update the sparse sources list.
    const bool wasSource = BlockRegistry::isLightSourceFast(oldId);
    const bool isSource = BlockRegistry::isLightSourceFast(newId);

    if (wasSource || isSource) {
        const std::size_t idx = static_cast<std::size_t>(localX) + static_cast<std::size_t>(localZ) * Chunk::SIZE_X +
                                static_cast<std::size_t>(y) * Chunk::SIZE_X * Chunk::SIZE_Z;

        if (wasSource) {
            // Remove old source entry.
            cache.sources.erase(std::remove_if(cache.sources.begin(), cache.sources.end(),
                                               [idx](const LightSourceEntry& e) { return e.packedIndex == idx; }),
                                cache.sources.end());
        }

        if (isSource) {
            const uint8_t level = BlockRegistry::getLightLevelFast(newId);
            if (level > 0) {
                cache.sources.push_back({static_cast<uint16_t>(idx), level});
            }
        }

        rebuildBlockLightFromSources(cache.sources, cache.packed);
    }
}

std::vector<BlockID> LightService::captureBlockSnapshot(const Chunk& chunk) {
    std::vector<BlockID> blocks;
    blocks.resize(Chunk::BLOCK_COUNT);

    for (int y = 0; y < Chunk::SIZE_Y; ++y) {
        for (int z = 0; z < Chunk::SIZE_Z; ++z) {
            for (int x = 0; x < Chunk::SIZE_X; ++x) {
                blocks[Chunk::toIndex(x, y, z)] = BlockStateRegistry::getBlockId(chunk.getBlock(x, y, z));
            }
        }
    }

    return blocks;
}

std::vector<uint8_t> LightService::capturePackedLightSnapshot(const Chunk& chunk) {
    std::vector<uint8_t> packed;
    packed.resize(Chunk::BLOCK_COUNT);

    for (int y = 0; y < Chunk::SIZE_Y; ++y) {
        for (int z = 0; z < Chunk::SIZE_Z; ++z) {
            for (int x = 0; x < Chunk::SIZE_X; ++x) {
                packed[Chunk::toIndex(x, y, z)] = chunk.getPackedLight(x, y, z);
            }
        }
    }

    return packed;
}

std::vector<BorderUpdateBatch> LightService::collectBoundaryInputs(const LightChunkState& state) {
    return collectBoundaryInputs(state.boundaryCache);
}

std::vector<BorderUpdateBatch>
LightService::collectBoundaryInputs(const std::array<std::optional<BorderUpdateBatch>, 4>& cache) {
    std::vector<BorderUpdateBatch> batches;
    batches.reserve(cache.size());
    for (const auto& cachedBatch : cache) {
        if (cachedBatch.has_value()) {
            batches.push_back(*cachedBatch);
        }
    }
    return batches;
}

void LightService::clearBoundaryInputs(std::array<std::optional<BorderUpdateBatch>, 4>& cache) {
    for (auto& entry : cache) {
        entry.reset();
    }
}
