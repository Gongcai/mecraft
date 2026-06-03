#include "World.h"
#include "WorldRaycast.h"
#include "../save/SaveManager.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iterator>
#include "engine//platform/Time.h"
#include "block/BlockSelection.h"
#include "fluid/FluidRegistry.h"
#include "fluid/FluidState.h"

namespace {
int worldToChunkCoord(const int world, const int chunkSize) {
    // floor-divide for negative coordinates
    return static_cast<int>(std::floor(static_cast<float>(world) / static_cast<float>(chunkSize)));
}

void markChunkSubChunkAndVerticalNeighborsDirty(Chunk& chunk, const int scy, const int localY) {
    chunk.markSubChunkDirty(scy);
    if (localY == 0) {
        chunk.markSubChunkDirty(scy - 1);
    }
    if (localY == Chunk::SUB_CHUNK_SIZE - 1) {
        chunk.markSubChunkDirty(scy + 1);
    }
}

bool canWaterOccupyBlockLayer(const StateID state) {
    const FluidDesc& waterDesc = FluidRegistry::get(FluidKind::Water);
    return FluidState::canReplace(waterDesc, state) || FluidState::canCoexist(waterDesc, state);
}

bool changesFluidPathing(const StateID oldState, const StateID newState) {
    return canWaterOccupyBlockLayer(oldState) != canWaterOccupyBlockLayer(newState);
}

StateID normalizeFluidBlockState(const StateID stateId) {
    if (BlockIds::WATER != BlockIds::AIR && stateId == BlockIds::WATER) {
        return FluidState::makeWater(0, false);
    }
    return stateId;
}

bool isWithinChunkRenderDistance(const int cx,
                                 const int cz,
                                 const int playerChunkX,
                                 const int playerChunkZ,
                                 const int renderDistance) {
    const int dx = cx - playerChunkX;
    const int dz = cz - playerChunkZ;
    return dx * dx + dz * dz <= renderDistance * renderDistance;
}

bool rayIntersectsAabb(const glm::vec3& rayOrigin,
                       const glm::vec3& rayDir,
                       const glm::vec3& boxMin,
                       const glm::vec3& boxMax,
                       const float maxDist,
                       float& tHit,
                       glm::ivec3& normal) {
    constexpr float kEpsilon = 1e-6f;
    float tMin = 0.0f;
    float tMax = maxDist;
    glm::ivec3 enterNormal(0);

    const auto testAxis = [&](const float origin,
                              const float dir,
                              const float minValue,
                              const float maxValue,
                              const glm::ivec3& negNormal,
                              const glm::ivec3& posNormal) {
        if (std::abs(dir) < kEpsilon) {
            return origin >= minValue && origin <= maxValue;
        }

        float t1 = (minValue - origin) / dir;
        float t2 = (maxValue - origin) / dir;
        glm::ivec3 axisNormal = negNormal;
        if (t1 > t2) {
            std::swap(t1, t2);
            axisNormal = posNormal;
        }

        if (t1 > tMin) {
            tMin = t1;
            enterNormal = axisNormal;
        }
        tMax = std::min(tMax, t2);
        return tMin <= tMax;
    };

    if (!testAxis(rayOrigin.x, rayDir.x, boxMin.x, boxMax.x,
                  glm::ivec3(-1, 0, 0), glm::ivec3(1, 0, 0))) {
        return false;
    }
    if (!testAxis(rayOrigin.y, rayDir.y, boxMin.y, boxMax.y,
                  glm::ivec3(0, -1, 0), glm::ivec3(0, 1, 0))) {
        return false;
    }
    if (!testAxis(rayOrigin.z, rayDir.z, boxMin.z, boxMax.z,
                  glm::ivec3(0, 0, -1), glm::ivec3(0, 0, 1))) {
        return false;
    }

    if (tMax < 0.0f || tMin > maxDist) {
        return false;
    }

    tHit = std::max(0.0f, tMin);
    normal = (tMin > 0.0f) ? enterNormal : glm::ivec3(0);
    return true;
}
}
void World::init(uint32_t seed) {
    m_seed = seed;
    m_terrainGen.init(seed, m_flatSurfaceY);
    m_chunks.clear();
    m_loadQueue.clear();
    m_generationInFlight.clear();
    {
        std::lock_guard<std::mutex> lock(m_completedGenMutex);
        m_completedGenQueue.clear();
    }
    m_fluidSystem.reset();
    m_neighborUpdateQueue.clear();
    m_ticketManager.reset();
    m_ticketManager.setViewRadius(m_renderDistance);
    m_ticketManager.setSimulationRadius(8);
    ++m_activeChunkRevision;
    m_lightService = std::make_unique<LightService>(*this);
    m_lightService->setLightChangeCallback(m_lightChangeCallback);
    m_lightService->start(m_threadPool);
    m_dayNightSystem.setTimeOfDay(300.0f); // Default to mid-day
}

void World::update(const glm::vec3& playerPos) {
    m_dayNightSystem.update(static_cast<float>(Time::deltaTime));
    m_weatherSystem.update(static_cast<float>(Time::deltaTime));

    const int playerChunkX = worldToChunkCoord(static_cast<int>(std::floor(playerPos.x)), Chunk::SIZE_X);
    const int playerChunkZ = worldToChunkCoord(static_cast<int>(std::floor(playerPos.z)), Chunk::SIZE_Z);

    // Update ticket manager with player position
    m_ticketManager.updatePlayerPosition(playerChunkX, playerChunkZ);

    // Unload chunks outside unload radius (with hysteresis)
    std::vector<int64_t> toUnload;
    for (const auto& pair : m_chunks) {
        const int cx = static_cast<int>(pair.first >> 32);
        const int cz = static_cast<int>(static_cast<int32_t>(pair.first & 0xFFFFFFFF));
        if (m_ticketManager.shouldUnload(cx, cz)) {
            toUnload.push_back(pair.first);
        }
    }
    for (const int64_t key : toUnload) {
        const int cx = static_cast<int>(key >> 32);
        const int cz = static_cast<int>(static_cast<int32_t>(key & 0xFFFFFFFF));
        unloadChunk(cx, cz);
    }

    // Get chunks to load from ticket manager (sorted by distance)
    // Build set of already loaded + in-flight chunks
    std::unordered_set<int64_t> loadedKeys;
    for (const auto& pair : m_chunks) {
        loadedKeys.insert(pair.first);
    }
    for (const int64_t key : m_generationInFlight) {
        loadedKeys.insert(key);
    }

    const auto chunksToLoad = m_ticketManager.getChunksToLoad(
        kMaxChunkLoadSubmitsPerFrame * 4,  // Look ahead a bit for prioritization
        loadedKeys);

    // Submit chunk generation jobs from the prioritized list
    int submitted = 0;
    for (const auto& pos : chunksToLoad) {
        if (submitted >= kMaxChunkLoadSubmitsPerFrame) {
            break;
        }
        if (static_cast<int>(m_generationInFlight.size()) >= kMaxGenerationInFlight) {
            break;
        }

        submitChunkLoad(pos.x, pos.y);
        ++submitted;
    }

    // Finalize completed generation results on the main thread with a small
    // frame budget. Neighbor linking and initial light queuing can dirty many
    // chunks, so batching every completed generation result at once causes
    // visible frame spikes while moving into new terrain.
    {
        std::vector<std::shared_ptr<Chunk>> completed;
        {
            std::lock_guard<std::mutex> lock(m_completedGenMutex);
            completed.swap(m_completedGenQueue);
        }
        const auto finalizeStart = std::chrono::steady_clock::now();
        std::vector<std::shared_ptr<Chunk>> deferred;
        deferred.reserve(completed.size());

        int finalized = 0;
        for (auto& chunk : completed) {
            const double elapsedMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - finalizeStart).count();
            if (finalized >= kMaxChunkLoadFinalizesPerFrame ||
                elapsedMs >= kChunkLoadFinalizeTimeBudgetMs) {
                deferred.push_back(std::move(chunk));
                continue;
            }

            const int64_t key = chunkKey(chunk->m_chunkX, chunk->m_chunkZ);
            m_generationInFlight.erase(key);
            finalizeChunkLoad(std::move(chunk));
            ++finalized;
        }

        if (!deferred.empty()) {
            std::lock_guard<std::mutex> lock(m_completedGenMutex);
            m_completedGenQueue.insert(m_completedGenQueue.begin(),
                                       std::make_move_iterator(deferred.begin()),
                                       std::make_move_iterator(deferred.end()));
        }
    }

    if (m_lightService) {
        const int dirtyCount = m_lightService->countDirtyChunks();
        const int completedDepth = m_lightService->completedCount();

        // Scale submit budget with load, back off when the completed queue is deep.
        int submitBudget = 6;
        if (completedDepth > 48) {
            submitBudget = 0;                // backpressure: let drain catch up
        } else if (dirtyCount > 50) {
            submitBudget = 10;               // many dirty chunks, increase throughput
        } else if (dirtyCount < 5) {
            submitBudget = 3;                // low load, conserve resources
        }

        // Drain more aggressively when results are piling up.
        const int mergeBudget = (completedDepth > 32) ? 12 : 6;
        const float mergeTimeBudgetMs = (completedDepth > 32) ? 1.5f : 0.75f;

        m_lightService->submitJobs(playerPos, submitBudget);
        m_lightService->drainCompleted(*this, mergeBudget, mergeTimeBudgetMs);
    }
}

BlockID World::getBlock(int x, int y, int z) const {
    if (y < 0 || y >= Chunk::SIZE_Y) return 0;

    const int chunkX = worldToChunkCoord(x, Chunk::SIZE_X);
    const int chunkZ = worldToChunkCoord(z, Chunk::SIZE_Z);

    auto it = m_chunks.find(chunkKey(chunkX, chunkZ));
    if (it != m_chunks.end()) {
        int localX = x - chunkX * Chunk::SIZE_X;
        int localZ = z - chunkZ * Chunk::SIZE_Z;
        return it->second->getBlock(localX, y, localZ);
    }
    return 0;
}

uint8_t World::getPackedLight(const int x, const int y, const int z) const {
    if (y < 0 || y >= Chunk::SIZE_Y) return 0;

    const int chunkX = worldToChunkCoord(x, Chunk::SIZE_X);
    const int chunkZ = worldToChunkCoord(z, Chunk::SIZE_Z);

    const auto it = m_chunks.find(chunkKey(chunkX, chunkZ));
    if (it == m_chunks.end()) {
        return 0;
    }

    const int localX = x - chunkX * Chunk::SIZE_X;
    const int localZ = z - chunkZ * Chunk::SIZE_Z;
    return it->second->getPackedLight(localX, y, localZ);
}

StateID World::getBlockState(const int x, const int y, const int z) const {
    return getBlock(x, y, z);
}

StateID World::getFluidState(const int x, const int y, const int z) const {
    if (y < 0 || y >= Chunk::SIZE_Y) return BlockIds::AIR;

    const int chunkX = worldToChunkCoord(x, Chunk::SIZE_X);
    const int chunkZ = worldToChunkCoord(z, Chunk::SIZE_Z);
    const auto it = m_chunks.find(chunkKey(chunkX, chunkZ));
    if (it == m_chunks.end()) {
        return BlockIds::AIR;
    }

    const int localX = x - chunkX * Chunk::SIZE_X;
    const int localZ = z - chunkZ * Chunk::SIZE_Z;
    const int scy = Chunk::toSubChunkIndex(y);
    const SubChunk* sc = it->second->getSubChunk(scy);
    if (!sc) {
        return BlockIds::AIR;
    }

    // First check the dedicated fluid layer
    const int localY = Chunk::toSubChunkLocalY(y);
    const BlockID fluidLayer = sc->getFluidLayer(localX, localY, localZ);
    if (fluidLayer != BlockIds::AIR) {
        return fluidLayer;
    }

    // Fallback: block layer may contain fluid (pure water positions)
    return FluidState::getFluidState(sc->getBlock(localX, localY, localZ));
}

FluidCellView World::getCombinedCell(const int x, const int y, const int z) const {
    if (y < 0 || y >= Chunk::SIZE_Y) return {};

    const int chunkX = worldToChunkCoord(x, Chunk::SIZE_X);
    const int chunkZ = worldToChunkCoord(z, Chunk::SIZE_Z);
    const auto it = m_chunks.find(chunkKey(chunkX, chunkZ));
    if (it == m_chunks.end()) {
        return {};
    }

    const int localX = x - chunkX * Chunk::SIZE_X;
    const int localZ = z - chunkZ * Chunk::SIZE_Z;
    const int scy = Chunk::toSubChunkIndex(y);
    const SubChunk* sc = it->second->getSubChunk(scy);
    if (!sc) {
        return {};
    }

    const int localY = Chunk::toSubChunkLocalY(y);
    const BlockID blockState = sc->getBlock(localX, localY, localZ);
    const BlockID fluidLayer = sc->getFluidLayer(localX, localY, localZ);

    const DecodedFluid blockFluid = FluidState::decode(blockState);
    if (blockFluid.kind != FluidKind::None) {
        // Pure fluid position (block layer IS the fluid)
        return FluidCellView{BlockIds::AIR, blockState};
    }
    // Block position (possibly waterlogged)
    return FluidCellView{blockState, fluidLayer};
}

BlockID World::sampleGeneratedBlock(const int x, const int y, const int z) const {
    if (y < 0 || y >= Chunk::SIZE_Y) {
        return 0;
    }

    return m_terrainGen.sampleBlock(x, y, z);
}

void World::setBlock(int x, int y, int z, BlockID id) {
    setBlockState(x, y, z, id);
}

void World::setFluidState(const int x, const int y, const int z, const StateID stateId) {
    if (y < 0 || y >= Chunk::SIZE_Y) return;
    const StateID normalizedStateId = normalizeFluidBlockState(stateId);

    const int chunkX = worldToChunkCoord(x, Chunk::SIZE_X);
    const int chunkZ = worldToChunkCoord(z, Chunk::SIZE_Z);
    const auto it = m_chunks.find(chunkKey(chunkX, chunkZ));
    if (it == m_chunks.end()) {
        return;
    }

    const int localX = x - chunkX * Chunk::SIZE_X;
    const int localZ = z - chunkZ * Chunk::SIZE_Z;
    Chunk& chunk = *it->second;
    const int scy = Chunk::toSubChunkIndex(y);
    SubChunk* sc = chunk.getOrCreateSubChunk(scy);
    if (!sc) {
        return;
    }

    const int localY = Chunk::toSubChunkLocalY(y);
    const DecodedFluid newFluid = FluidState::decode(normalizedStateId);
    const BlockID currentBlock = sc->getBlock(localX, localY, localZ);
    const DecodedFluid currentBlockFluid = FluidState::decode(currentBlock);

    if (currentBlockFluid.kind != FluidKind::None) {
        // Current block layer IS fluid (pure water position).
        // If new state is also fluid of same kind, update block layer directly.
        // If new state is air/no-fluid, clear block layer to air.
        const StateID targetBlockState = (newFluid.kind != FluidKind::None)
            ? normalizedStateId
            : BlockIds::AIR;
        setBlockState(x, y, z, targetBlockState);
        return;
    }

    // Block layer is a real block (possibly waterlogged).
    if (newFluid.kind == FluidKind::None) {
        // Removing fluid from this cell
        const BlockID oldFluid = sc->getFluidLayer(localX, localY, localZ);
        if (oldFluid == BlockIds::AIR) {
            return;  // Nothing to do
        }
        sc->setFluidLayer(localX, localY, localZ, BlockIds::AIR);
    } else {
        // Adding/updating fluid in a waterlogged cell
        if (BlockRegistry::getFast(currentBlock).allowsFluidCoexistence) {
            sc->setFluidLayer(localX, localY, localZ, normalizedStateId);
        } else {
            // Block doesn't allow fluid coexistence — replace the block with fluid
            setBlockState(x, y, z, normalizedStateId);
            return;
        }
    }

    // Mark dirty for remesh
    markChunkSubChunkAndVerticalNeighborsDirty(chunk, scy, localY);
    if (localX == 0) {
        auto nit = m_chunks.find(chunkKey(chunkX - 1, chunkZ));
        if (nit != m_chunks.end()) markChunkSubChunkAndVerticalNeighborsDirty(*nit->second, scy, localY);
    }
    if (localX == Chunk::SIZE_X - 1) {
        auto nit = m_chunks.find(chunkKey(chunkX + 1, chunkZ));
        if (nit != m_chunks.end()) markChunkSubChunkAndVerticalNeighborsDirty(*nit->second, scy, localY);
    }
    if (localZ == 0) {
        auto nit = m_chunks.find(chunkKey(chunkX, chunkZ - 1));
        if (nit != m_chunks.end()) markChunkSubChunkAndVerticalNeighborsDirty(*nit->second, scy, localY);
    }
    if (localZ == Chunk::SIZE_Z - 1) {
        auto nit = m_chunks.find(chunkKey(chunkX, chunkZ + 1));
        if (nit != m_chunks.end()) markChunkSubChunkAndVerticalNeighborsDirty(*nit->second, scy, localY);
    }

    m_fluidSystem.onBlockChanged(glm::ivec3(x, y, z));

    // Notify block change callback for waterlogged fluid changes
    // (pure fluid and block-replacement paths go through setBlockState, which already fires the callback)
    if (m_blockChangeCallback) {
        const BlockID currentBlockId = sc->getBlock(localX, localY, localZ);
        m_blockChangeCallback(x, y, z, currentBlockId);
    }

    // Mark chunk dirty for persistence
    markChunkSaveDirty(chunkX, chunkZ);
}

bool World::isChunkLoadedForBlock(const int x, const int y, const int z) const {
    if (y < 0 || y >= Chunk::SIZE_Y) {
        return false;
    }

    const int chunkX = worldToChunkCoord(x, Chunk::SIZE_X);
    const int chunkZ = worldToChunkCoord(z, Chunk::SIZE_Z);
    return m_chunks.find(chunkKey(chunkX, chunkZ)) != m_chunks.end();
}

void World::setBlockState(int x, int y, int z, StateID id) {
    if (y < 0 || y >= Chunk::SIZE_Y) return;
    id = normalizeFluidBlockState(id);

    const int chunkX = worldToChunkCoord(x, Chunk::SIZE_X);
    const int chunkZ = worldToChunkCoord(z, Chunk::SIZE_Z);

    auto it = m_chunks.find(chunkKey(chunkX, chunkZ));
    if (it == m_chunks.end()) {
        return;
    }

    const int localX = x - chunkX * Chunk::SIZE_X;
    const int localZ = z - chunkZ * Chunk::SIZE_Z;
    Chunk& chunk = *it->second;

    const int editedScy = Chunk::toSubChunkIndex(y);
    const int localY = Chunk::toSubChunkLocalY(y);
    const SubChunk* existingSubChunk = chunk.getSubChunk(editedScy);
    const BlockID oldId = chunk.getBlock(localX, y, localZ);
    const BlockID oldFluidLayer = existingSubChunk
        ? existingSubChunk->getFluidLayer(localX, localY, localZ)
        : BlockIds::AIR;

    StateID targetState = id;
    const bool uncoverFluidLayer =
        id == BlockIds::AIR &&
        oldFluidLayer != BlockIds::AIR &&
        FluidState::decode(oldId).kind == FluidKind::None &&
        FluidState::decode(oldFluidLayer).kind != FluidKind::None;
    if (uncoverFluidLayer) {
        targetState = oldFluidLayer;
    }

    if (oldId == targetState && !uncoverFluidLayer) {
        return;
    }

    if (m_lightService) {
        chunk.setBlockWithoutMeshDirty(localX, y, localZ, targetState);
        m_lightService->onBlockChanged(x, y, z, oldId, targetState);
    } else {
        chunk.setBlock(localX, y, localZ, targetState);
    }

    if (uncoverFluidLayer) {
        if (SubChunk* sc = chunk.getSubChunk(editedScy)) {
            sc->setFluidLayer(localX, localY, localZ, BlockIds::AIR);
        }
    }


    // Geometry edits must always trigger remesh, regardless of lighting pipeline.
    markChunkSubChunkAndVerticalNeighborsDirty(chunk, editedScy, localY);
    if (localX == 0) {
        auto nit = m_chunks.find(chunkKey(chunkX - 1, chunkZ));
        if (nit != m_chunks.end()) markChunkSubChunkAndVerticalNeighborsDirty(*nit->second, editedScy, localY);
    }
    if (localX == Chunk::SIZE_X - 1) {
        auto nit = m_chunks.find(chunkKey(chunkX + 1, chunkZ));
        if (nit != m_chunks.end()) markChunkSubChunkAndVerticalNeighborsDirty(*nit->second, editedScy, localY);
    }
    if (localZ == 0) {
        auto nit = m_chunks.find(chunkKey(chunkX, chunkZ - 1));
        if (nit != m_chunks.end()) markChunkSubChunkAndVerticalNeighborsDirty(*nit->second, editedScy, localY);
    }
    if (localZ == Chunk::SIZE_Z - 1) {
        auto nit = m_chunks.find(chunkKey(chunkX, chunkZ + 1));
        if (nit != m_chunks.end()) markChunkSubChunkAndVerticalNeighborsDirty(*nit->second, editedScy, localY);
    }

    m_fluidSystem.onBlockChanged(glm::ivec3(x, y, z), changesFluidPathing(oldId, targetState));

    // Enqueue 6 neighbors for support-rule validation (block tick physics).
    static constexpr glm::ivec3 kNeighborOffsets[6] = {
        { 1,  0,  0}, {-1,  0,  0},
        { 0,  1,  0}, { 0, -1,  0},
        { 0,  0,  1}, { 0,  0, -1},
    };
    for (const auto& off : kNeighborOffsets) {
        m_neighborUpdateQueue.enqueue(glm::ivec3(x, y, z) + off);
    }

    // Notify block change callback (used by GameServer for BlockUpdateBatch)
    if (m_blockChangeCallback) {
        m_blockChangeCallback(x, y, z, targetState);
    }

    // Mark chunk dirty for persistence
    markChunkSaveDirty(chunkX, chunkZ);
}

void World::setThreadPool(ThreadPool* pool) {
    m_threadPool = pool;
    if (m_lightService) {
        m_lightService->shutdown();
        m_lightService->setLightChangeCallback(m_lightChangeCallback);
        m_lightService->start(m_threadPool);
    }
}

void World::setLightChangeCallback(LightChangeCallback callback) {
    m_lightChangeCallback = std::move(callback);
    if (m_lightService) {
        m_lightService->setLightChangeCallback(m_lightChangeCallback);
    }
}

// ---------------------------------------------------------------------------
// Save system integration
// ---------------------------------------------------------------------------

void World::setSaveManager(save::SaveManager* saveManager) {
    m_saveManager = saveManager;
}

void World::markChunkSaveDirty(int cx, int cz) {
    m_dirtySaveChunks.insert(chunkKey(cx, cz));
}

void World::flushSaves() {
    if (!m_saveManager) return;

    // Submit all remaining dirty chunks for saving
    for (int64_t key : m_dirtySaveChunks) {
        int cx = static_cast<int>(key >> 32);
        int cz = static_cast<int>(static_cast<int32_t>(key & 0xFFFFFFFF));
        auto it = m_chunks.find(key);
        if (it != m_chunks.end()) {
            m_saveManager->submitSaveChunk(cx, cz, *it->second);
        }
    }
    m_dirtySaveChunks.clear();

    // Wait for all pending saves to complete
    m_saveManager->flushPendingSaves();
}

LightFrameStats World::getLightFrameStats() const {
    if (!m_lightService) {
        return {};
    }
    return m_lightService->getFrameStats();
}

RayHit World::raycast(const PhysicsInfo& ray, const float maxDist) const {
    return raycastWorldView(*this, ray, maxDist);
}

bool World::raycast(const PhysicsInfo& ray, const float maxDist, glm::ivec3& hitBlock, glm::ivec3& placeBlock) const {
    const RayHit hit = raycast(ray, maxDist);
    if (!hit.hit) {
        return false;
    }

    hitBlock = hit.blockPos;
    placeBlock = hit.blockPos + hit.normal;
    return true;
}

void World::setRenderDistance(int dist) {
    m_renderDistance = std::max(1, dist);
    m_ticketManager.setViewRadius(m_renderDistance);
}

void World::setSimulationDistance(int distance) {
    m_ticketManager.setSimulationRadius(std::max(1, distance));
}

int World::getSurfaceY(int x, int z) const {
    const int chunkX = worldToChunkCoord(x, Chunk::SIZE_X);
    const int chunkZ = worldToChunkCoord(z, Chunk::SIZE_Z);
    const auto it = m_chunks.find(chunkKey(chunkX, chunkZ));
    if (it != m_chunks.end()) {
        const int localX = x - chunkX * Chunk::SIZE_X;
        const int localZ = z - chunkZ * Chunk::SIZE_Z;
        for (int y = Chunk::SIZE_Y - 1; y >= 0; --y) {
            if (it->second->getBlock(localX, y, localZ) != 0) {
                return y;
            }
        }
        return 0;
    }

    return m_terrainGen.sampleSurfaceY(x, z);
}

TerrainBiome World::getBiome(int x, int z) const {
    return m_terrainGen.sampleBiome(x, z);
}

glm::ivec2 World::getChunkCoords(int worldX, int worldZ) const {
    return {
        worldToChunkCoord(worldX, Chunk::SIZE_X),
        worldToChunkCoord(worldZ, Chunk::SIZE_Z)
    };
}

const char* World::biomeToString(TerrainBiome biome) {
    switch (biome) {
        case TerrainBiome::Temperate:
            return "Temperate";
        case TerrainBiome::Arid:
            return "Arid";
        case TerrainBiome::Mountain:
            return "Mountain";
        case TerrainBiome::HighMountain:
            return "High Mountain";
        default:
            return "Unknown";
    }
}

int64_t World::chunkKey(int cx, int cz) {
    return (static_cast<int64_t>(cx) << 32) | (static_cast<int64_t>(cz) & 0xFFFFFFFF);
}

void World::submitChunkLoad(int cx, int cz) {
    int64_t key = chunkKey(cx, cz);
    if (m_chunks.find(key) != m_chunks.end()) return;
    if (m_generationInFlight.count(key)) return;
    if (!m_threadPool || !m_threadPool->isRunning()) {
        // No thread pool — fall back to synchronous load
        loadChunk(cx, cz);
        return;
    }

    m_generationInFlight.insert(key);

    // Try loading from disk first, fall back to terrain generation.
    // Both paths push to m_completedGenQueue which is consumed by finalizeChunkLoad().
    auto chunk = std::make_shared<Chunk>(cx, cz);
    TerrainGenerator* terrainGen = &m_terrainGen;
    save::SaveManager* sm = m_saveManager;

    m_threadPool->submit([chunk, terrainGen, sm, this]() {
        bool loadedFromDisk = false;

        if (sm && sm->chunkFileExists(chunk->m_chunkX, chunk->m_chunkZ)) {
            auto loaded = sm->tryLoadChunk(chunk->m_chunkX, chunk->m_chunkZ);
            if (loaded) {
                loaded->seedInitialLightMap();
                {
                    std::lock_guard<std::mutex> lock(m_completedGenMutex);
                    m_completedGenQueue.push_back(std::move(loaded));
                }
                loadedFromDisk = true;
            }
        }

        if (!loadedFromDisk) {
            terrainGen->generateChunk(*chunk);
            chunk->seedInitialLightMap();
            {
                std::lock_guard<std::mutex> lock(m_completedGenMutex);
                m_completedGenQueue.push_back(chunk);
            }
        }
    }, 0);
}

void World::finalizeChunkLoad(std::shared_ptr<Chunk> chunk) {
    const int cx = chunk->m_chunkX;
    const int cz = chunk->m_chunkZ;
    const int64_t key = chunkKey(cx, cz);

    // Guard against duplicate finalization (e.g. if loadChunk was called directly)
    if (m_chunks.find(key) != m_chunks.end()) return;

    m_chunks[key] = std::move(chunk);
    ++m_activeChunkRevision;

    // Wire up neighbor pointers for the new chunk and its existing neighbors
    Chunk* cur = m_chunks[key].get();
    auto markNeighborBorderDirty = [&](Chunk& neighbor) {
        for (int scy = 0; scy < Chunk::NUM_SUB_CHUNKS; ++scy) {
            // Only mark dirty when both sides have content — if the new chunk
            // has no subchunk here, its border is all-air and the neighbor's
            // faces are already correct. If the neighbor has no subchunk, there
            // is nothing to remesh.
            if (cur->getSubChunk(scy) && neighbor.getSubChunk(scy)) {
                neighbor.markSubChunkDirty(scy);
            }
        }
    };
    auto linkNeighbor = [&](int ncx, int ncz, int selfIdx, int neighborIdx) {
        auto it = m_chunks.find(chunkKey(ncx, ncz));
        if (it == m_chunks.end()) {
            return;
        }

        Chunk* neighbor = it->second.get();
        cur->neighbors[selfIdx] = neighbor;
        neighbor->neighbors[neighborIdx] = cur;
        cur->linkExistingSubChunksWithNeighbor(selfIdx);
        markNeighborBorderDirty(*neighbor);
    };
    linkNeighbor(cx + 1, cz, 0, 1);
    linkNeighbor(cx - 1, cz, 1, 0);
    linkNeighbor(cx, cz + 1, 2, 3);
    linkNeighbor(cx, cz - 1, 3, 2);

    // Initialize lighting after terrain generation and neighbor linking
    if (m_lightService) {
        m_lightService->onChunkLoaded(m_chunks[key]);
    }
}

void World::loadChunk(int cx, int cz) {
    int64_t key = chunkKey(cx, cz);
    if (m_chunks.find(key) != m_chunks.end()) return;

    // Try loading from disk first, fall back to terrain generation
    std::shared_ptr<Chunk> chunk;
    if (m_saveManager && m_saveManager->chunkFileExists(cx, cz)) {
        chunk = m_saveManager->tryLoadChunk(cx, cz);
    }
    if (!chunk) {
        chunk = std::make_shared<Chunk>(cx, cz);
        m_terrainGen.generateChunk(*chunk);
    }
    chunk->seedInitialLightMap();

    m_chunks[key] = std::move(chunk);
    ++m_activeChunkRevision;

    // Wire up neighbor pointers for the new chunk and its existing neighbors
    Chunk* cur = m_chunks[key].get();
    auto markNeighborBorderDirty = [&](Chunk& neighbor) {
        for (int scy = 0; scy < Chunk::NUM_SUB_CHUNKS; ++scy) {
            if (cur->getSubChunk(scy) && neighbor.getSubChunk(scy)) {
                neighbor.markSubChunkDirty(scy);
            }
        }
    };
    auto linkNeighbor = [&](int ncx, int ncz, int selfIdx, int neighborIdx) {
        auto it = m_chunks.find(chunkKey(ncx, ncz));
        if (it == m_chunks.end()) {
            return;
        }

        Chunk* neighbor = it->second.get();
        cur->neighbors[selfIdx] = neighbor;
        neighbor->neighbors[neighborIdx] = cur;
        cur->linkExistingSubChunksWithNeighbor(selfIdx);
        markNeighborBorderDirty(*neighbor);
    };
    linkNeighbor(cx + 1, cz, 0, 1);
    linkNeighbor(cx - 1, cz, 1, 0);
    linkNeighbor(cx, cz + 1, 2, 3);
    linkNeighbor(cx, cz - 1, 3, 2);

    // Initialize lighting after terrain generation and neighbor linking
    if (m_lightService) {
        m_lightService->onChunkLoaded(m_chunks[key]);
    }

}

void World::unloadChunk(int cx, int cz) {
    int64_t key = chunkKey(cx, cz);
    auto it = m_chunks.find(key);
    if (it == m_chunks.end()) return;

    // Save dirty chunk to disk before unloading
    if (m_saveManager && m_dirtySaveChunks.count(key)) {
        m_saveManager->submitSaveChunk(cx, cz, *it->second);
        m_dirtySaveChunks.erase(key);
    }

    if (m_lightService) {
        m_lightService->onChunkUnloaded(it->first);
    }

    Chunk* chunk = it->second.get();
    for (int direction = 0; direction < 4; ++direction) {
        Chunk* neighbor = chunk->neighbors[direction];
        if (!neighbor) {
            continue;
        }

        for (int scy = 0; scy < Chunk::NUM_SUB_CHUNKS; ++scy) {
            // Only mark neighbor dirty if the unloading chunk had content at
            // this level that could have been hiding the neighbor's border faces.
            if (chunk->getSubChunk(scy) && neighbor->getSubChunk(scy)) {
                neighbor->markSubChunkDirty(scy);
            }
        }
        chunk->unlinkExistingSubChunksFromNeighbor(direction);
    }

    if (chunk->neighbors[0]) chunk->neighbors[0]->neighbors[1] = nullptr;
    if (chunk->neighbors[1]) chunk->neighbors[1]->neighbors[0] = nullptr;
    if (chunk->neighbors[2]) chunk->neighbors[2]->neighbors[3] = nullptr;
    if (chunk->neighbors[3]) chunk->neighbors[3]->neighbors[2] = nullptr;

    m_chunks.erase(it);
    ++m_activeChunkRevision;
}


size_t World::getTotalVertexCount() const {
    size_t total = 0;
    for (const auto& pair : m_chunks) {
        if (pair.second) {
            for (int scy = 0; scy < Chunk::NUM_SUB_CHUNKS; ++scy) {
                const SubChunk* sc = pair.second->getSubChunk(scy);
                if (sc) {
                    total += sc->getMesh().vertexCount;
                    total += sc->getMesh().cutoutVertexCount;
                    total += sc->getMesh().cutoutDistanceVertexCount;
                    total += sc->getMesh().transparentVertexCount;
                }
            }
        }
    }
    return total;
}

void World::updateLoadQueue(int playerChunkX, int playerChunkZ) {
    m_loadQueue.clear();

    for (int dx = -m_renderDistance; dx <= m_renderDistance; ++dx) {
        for (int dz = -m_renderDistance; dz <= m_renderDistance; ++dz) {
            if (dx * dx + dz * dz > m_renderDistance * m_renderDistance) {
                continue;
            }
            int cx = playerChunkX + dx;
            int cz = playerChunkZ + dz;
            const int64_t key = chunkKey(cx, cz);
            if (m_chunks.find(key) == m_chunks.end() && !m_generationInFlight.count(key)) {
                m_loadQueue.push_back(glm::ivec2(cx, cz));
            }
        }
    }

    std::sort(m_loadQueue.begin(), m_loadQueue.end(),
              [playerChunkX, playerChunkZ](const glm::ivec2& a, const glm::ivec2& b) {
                  int distA = (a.x - playerChunkX) * (a.x - playerChunkX) +
                              (a.y - playerChunkZ) * (a.y - playerChunkZ);
                  int distB = (b.x - playerChunkX) * (b.x - playerChunkX) +
                              (b.y - playerChunkZ) * (b.y - playerChunkZ);
                  return distA > distB;
              });
}
