#include "LightService.h"

#include <cmath>
#include <chrono>
#include <limits>
#include <utility>

#include "Chunk.h"
#include "LightSolver.h"
#include "../thread/ThreadPool.h"
#include "World.h"

namespace {
std::shared_ptr<const Chunk> findSharedByRawPtr(const World& world, const Chunk* raw) {
    if (raw == nullptr) {
        return nullptr;
    }

    const int64_t key = World::chunkKey(raw->m_chunkX, raw->m_chunkZ);
    auto it = world.getActiveChunks().find(key);
    if (it == world.getActiveChunks().end() || !it->second || it->second.get() != raw) {
        return nullptr;
    }

    return std::static_pointer_cast<const Chunk>(it->second);
}

int reasonBias(const LightDirtyReason reason) {
    switch (reason) {
        case LightDirtyReason::BlockChanged:
            return 0;
        case LightDirtyReason::ChunkLoaded:
            return 500;
        case LightDirtyReason::NeighborBoundary:
        default:
            return 1000;
    }
}
}

LightService::LightService(World& world)
    : m_world(world) {}

LightService::~LightService() = default;

void LightService::start(ThreadPool* pool) {
    m_pool = pool;
    m_running = true;
}

void LightService::shutdown() {
    m_running = false;
    m_pool = nullptr;
    m_frameStats = {};

    std::lock_guard<std::mutex> stateLock(m_stateMutex);
    m_chunkStates.clear();

    std::lock_guard<std::mutex> completedLock(m_completedMutex);
    m_completed = {};
}

void LightService::onChunkLoaded(const std::shared_ptr<Chunk>& chunk) {
    if (!m_running || !chunk) {
        return;
    }

    const int64_t key = World::chunkKey(chunk->m_chunkX, chunk->m_chunkZ);
    markChunkDirty(key, LightDirtyReason::ChunkLoaded);

    // New chunk and loaded neighbors both need one boundary synchronization pass.
    for (int direction = 0; direction < 4; ++direction) {
        Chunk* neighbor = chunk->neighbors[direction];
        if (!neighbor) {
            continue;
        }
        markChunkDirty(World::chunkKey(neighbor->m_chunkX, neighbor->m_chunkZ),
                       LightDirtyReason::NeighborBoundary);
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
            neighborState.pendingPreviousBoundaryCache[direction] = neighborState.boundaryCache[direction];
            neighborState.boundaryCache[direction].reset();
            neighborState.pendingBoundaryChanged[direction] = true;
            neighborState.dirty = true;
            if (neighborState.reason != LightDirtyReason::BlockChanged) {
                neighborState.reason = LightDirtyReason::NeighborBoundary;
            }

            if (!neighborState.inFlight && !neighborState.queued) {
                neighborState.queued = true;
                neighbor->setLightQueued(true);
            }
        }
    }

    m_chunkStates.erase(chunkKey);
}

void LightService::onBlockChanged(const int wx, const int wy, const int wz,
                                  const BlockID oldId, const BlockID newId) {
    if (!m_running) {
        return;
    }

    const glm::ivec2 chunkCoords = m_world.getChunkCoords(wx, wz);
    const int64_t key = World::chunkKey(chunkCoords.x, chunkCoords.y);
    auto it = m_world.getActiveChunks().find(key);
    if (it != m_world.getActiveChunks().end() && it->second) {
        const int localX = wx - chunkCoords.x * Chunk::SIZE_X;
        const int localZ = wz - chunkCoords.y * Chunk::SIZE_Z;
        it->second->recalcHeightMap(localX, localZ);
        it->second->bumpLightRevision();

        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            LightChunkState& state = m_chunkStates[key];
            state.pendingBlockChanges.push_back({
                static_cast<uint8_t>(localX),
                static_cast<uint8_t>(wy),
                static_cast<uint8_t>(localZ),
                oldId,
                newId
            });
        }

        for (int direction = 0; direction < 4; ++direction) {
            Chunk* neighbor = it->second->neighbors[direction];
            if (!neighbor) {
                continue;
            }
            markChunkDirty(World::chunkKey(neighbor->m_chunkX, neighbor->m_chunkZ),
                           LightDirtyReason::NeighborBoundary);
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

    if (!m_running || m_pool == nullptr || submitBudget <= 0) {
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
                    state.inFlight = true;
                    state.queued = false;
                    state.dirty = false;

                    job.chunkKey = chunkKey;
                    job.revision = chunkIt->second->getLightRevision();
                    job.reason = pickedReason;
                    job.chunk = chunkIt->second;
                    job.neighborPosX = findSharedByRawPtr(m_world, chunkIt->second->neighbors[0]);
                    job.neighborNegX = findSharedByRawPtr(m_world, chunkIt->second->neighbors[1]);
                    job.neighborPosZ = findSharedByRawPtr(m_world, chunkIt->second->neighbors[2]);
                    job.neighborNegZ = findSharedByRawPtr(m_world, chunkIt->second->neighbors[3]);
                    job.blockSnapshot = captureBlockSnapshot(*chunkIt->second);
                    job.heightMapSnapshot = captureHeightMapSnapshot(*chunkIt->second);
                    job.blockChanges = std::move(state.pendingBlockChanges);
                    state.pendingBlockChanges.clear();
                    job.previousInbox = collectBoundaryInputs(state.pendingPreviousBoundaryCache);
                    clearBoundaryInputs(state.pendingPreviousBoundaryCache);
                    job.changedBoundaryDirections = state.pendingBoundaryChanged;
                    state.pendingBoundaryChanged.fill(false);
                    job.inbox = collectBoundaryInputs(state);
                    job.packedLightSnapshot = capturePackedLightSnapshot(*chunkIt->second);

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

        m_pool->submit([this, job = std::move(job)]() {
            CompletedTicket ticket;
            ticket.result = LightSolver::solve(job);
            onWorkerCompleted(std::move(ticket));
        }, reasonBias(pickedReason));
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

void LightService::drainCompleted(World& world, const int mergeBudget) {
    if (!m_running) {
        return;
    }

    if (mergeBudget <= 0) {
        return;
    }

    auto mergeStart = std::chrono::steady_clock::now();
    int merged = 0;
    while (merged < mergeBudget) {
        CompletedTicket ticket;
        {
            std::lock_guard<std::mutex> lock(m_completedMutex);
            if (m_completed.empty()) {
                break;
            }
            ticket = std::move(m_completed.front());
            m_completed.pop();
        }

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
                    state.queued = true;
                    ++m_frameStats.requeued;
                    if (chunkIt != world.getActiveChunks().end() && chunkIt->second) {
                        chunkIt->second->setLightQueued(true);
                    }
                }
                continue;
            }

            if (!ticket.result.selfDelta.packedLight.empty()) {
                chunkIt->second->replacePackedLight(ticket.result.selfDelta.packedLight.data(),
                                                    ticket.result.selfDelta.packedLight.size(),
                                                    nullptr);
            }

            for (const BorderUpdateBatch& batch : ticket.result.outgoing) {
                auto neighborChunkIt = world.getActiveChunks().find(batch.targetChunkKey);
                if (neighborChunkIt == world.getActiveChunks().end() || !neighborChunkIt->second) {
                    continue;
                }

                LightChunkState& neighborState = m_chunkStates[batch.targetChunkKey];
                if (batch.fromDirection < neighborState.boundaryCache.size()) {
                    neighborState.pendingPreviousBoundaryCache[batch.fromDirection] =
                        neighborState.boundaryCache[batch.fromDirection];
                    neighborState.boundaryCache[batch.fromDirection] = batch;
                    neighborState.pendingBoundaryChanged[batch.fromDirection] = true;
                }
                neighborState.dirty = true;
                if (neighborState.reason != LightDirtyReason::BlockChanged) {
                    neighborState.reason = LightDirtyReason::NeighborBoundary;
                }
                if (!neighborState.inFlight && !neighborState.queued) {
                    neighborState.queued = true;
                    neighborChunkIt->second->setLightQueued(true);
                }
                if (batch.dirtySubChunkMask != 0) {
                    for (int scy = 0; scy < Chunk::NUM_SUB_CHUNKS; ++scy) {
                        if ((batch.dirtySubChunkMask & (1u << scy)) != 0u) {
                            neighborChunkIt->second->markSubChunkDirty(scy);
                        }
                    }
                }
                ++m_frameStats.boundarySync;
            }

            state.inFlight = false;
            state.queued = false;
            shouldRequeue = state.dirty;
            if (shouldRequeue) {
                state.queued = true;
                ++m_frameStats.requeued;
                chunkIt->second->setLightQueued(true);
            }
        }
    }

    m_frameStats.mergeMs = static_cast<float>(std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - mergeStart).count());

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

LightFrameStats LightService::getFrameStats() const {
    return m_frameStats;
}

void LightService::markChunkDirty(const int64_t chunkKey, const LightDirtyReason reason) {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    LightChunkState& state = m_chunkStates[chunkKey];
    state.dirty = true;

    if (state.reason != LightDirtyReason::BlockChanged) {
        if (reason == LightDirtyReason::BlockChanged ||
            (reason == LightDirtyReason::ChunkLoaded && state.reason == LightDirtyReason::NeighborBoundary)) {
            state.reason = reason;
        }
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
    m_completed.push(std::move(ticket));
}

std::vector<BlockID> LightService::captureBlockSnapshot(const Chunk& chunk) {
    std::vector<BlockID> blocks;
    blocks.resize(Chunk::BLOCK_COUNT);

    for (int y = 0; y < Chunk::SIZE_Y; ++y) {
        for (int z = 0; z < Chunk::SIZE_Z; ++z) {
            for (int x = 0; x < Chunk::SIZE_X; ++x) {
                blocks[Chunk::toIndex(x, y, z)] = chunk.getBlock(x, y, z);
            }
        }
    }

    return blocks;
}

std::vector<int16_t> LightService::captureHeightMapSnapshot(const Chunk& chunk) {
    std::vector<int16_t> heights;
    heights.resize(static_cast<size_t>(Chunk::SIZE_X) * static_cast<size_t>(Chunk::SIZE_Z));

    for (int z = 0; z < Chunk::SIZE_Z; ++z) {
        for (int x = 0; x < Chunk::SIZE_X; ++x) {
            const size_t idx = static_cast<size_t>(x) + static_cast<size_t>(z) * static_cast<size_t>(Chunk::SIZE_X);
            heights[idx] = static_cast<int16_t>(chunk.getHeightMap(x, z));
        }
    }

    return heights;
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

std::vector<BorderUpdateBatch> LightService::collectBoundaryInputs(
    const std::array<std::optional<BorderUpdateBatch>, 4>& cache) {
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









