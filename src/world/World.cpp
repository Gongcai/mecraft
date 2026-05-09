#include "World.h"
#include <algorithm>
#include <cmath>
#include "../core/Time.h"
#include "BlockSelection.h"
#include "FluidRegistry.h"
#include "FluidState.h"

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
    ++m_activeChunkRevision;
    m_lightService = std::make_unique<LightService>(*this);
    m_lightService->start(m_threadPool);
    m_dayNightSystem.setTimeOfDay(300.0f); // Default to mid-day
}

void World::update(const glm::vec3& playerPos) {
    m_dayNightSystem.update(static_cast<float>(Time::deltaTime));

    const int playerChunkX = worldToChunkCoord(static_cast<int>(std::floor(playerPos.x)), Chunk::SIZE_X);
    const int playerChunkZ = worldToChunkCoord(static_cast<int>(std::floor(playerPos.z)), Chunk::SIZE_Z);

    updateLoadQueue(playerChunkX, playerChunkZ);

    std::vector<int64_t> toUnload;
    for (const auto& pair : m_chunks) {
        int cx = static_cast<int>(pair.first >> 32);
        int cz = static_cast<int>(pair.first & 0xFFFFFFFF);
        if (!isWithinChunkRenderDistance(cx, cz, playerChunkX, playerChunkZ, m_renderDistance)) {
            toUnload.push_back(pair.first);
        }
    }
    for (int64_t key : toUnload) {
        const int cx = static_cast<int>(key >> 32);
        const int cz = static_cast<int>(key & 0xFFFFFFFF);
        unloadChunk(cx, cz);
    }

    // Submit chunk generation jobs to thread pool (async terrain generation).
    constexpr int kMaxChunkLoadsPerFrame = 4;
    int submitted = 0;
    while (!m_loadQueue.empty() && submitted < kMaxChunkLoadsPerFrame) {
        if (static_cast<int>(m_generationInFlight.size()) >= kMaxGenerationInFlight) {
            break;
        }

        auto pos = m_loadQueue.back();
        m_loadQueue.pop_back();
        submitChunkLoad(pos.x, pos.y);
        submitted++;
    }

    // Finalize completed generation results on the main thread.
    {
        std::vector<std::shared_ptr<Chunk>> completed;
        {
            std::lock_guard<std::mutex> lock(m_completedGenMutex);
            completed.swap(m_completedGenQueue);
        }
        for (auto& chunk : completed) {
            const int64_t key = chunkKey(chunk->m_chunkX, chunk->m_chunkZ);
            m_generationInFlight.erase(key);
            finalizeChunkLoad(std::move(chunk));
        }
    }

    if (m_lightService) {
        const int dirtyCount = m_lightService->countDirtyChunks();
        const int completedDepth = m_lightService->completedCount();

        // Scale submit budget with load, back off when the completed queue is deep.
        int submitBudget = 8;
        if (completedDepth > 48) {
            submitBudget = 0;                // backpressure: let drain catch up
        } else if (dirtyCount > 50) {
            submitBudget = 16;               // many dirty chunks, increase throughput
        } else if (dirtyCount < 5) {
            submitBudget = 4;                // low load, conserve resources
        }

        // Drain more aggressively when results are piling up.
        int mergeBudget = (completedDepth > 32) ? 64 : 32;

        m_lightService->submitJobs(playerPos, submitBudget);
        m_lightService->drainCompleted(*this, mergeBudget);
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

    const int chunkX = worldToChunkCoord(x, Chunk::SIZE_X);
    const int chunkZ = worldToChunkCoord(z, Chunk::SIZE_Z);
    const auto it = m_chunks.find(chunkKey(chunkX, chunkZ));
    if (it != m_chunks.end()) {
        const int localX = x - chunkX * Chunk::SIZE_X;
        const int localZ = z - chunkZ * Chunk::SIZE_Z;
        return it->second->getBlock(localX, y, localZ);
    }

    return m_terrainGen.sampleBlock(x, y, z);
}

void World::setBlock(int x, int y, int z, BlockID id) {
    setBlockState(x, y, z, id);
}

void World::setFluidState(const int x, const int y, const int z, const StateID stateId) {
    if (y < 0 || y >= Chunk::SIZE_Y) return;

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
    const DecodedFluid newFluid = FluidState::decode(stateId);
    const BlockID currentBlock = sc->getBlock(localX, localY, localZ);
    const DecodedFluid currentBlockFluid = FluidState::decode(currentBlock);

    if (currentBlockFluid.kind != FluidKind::None) {
        // Current block layer IS fluid (pure water position).
        // If new state is also fluid of same kind, update block layer directly.
        // If new state is air/no-fluid, clear block layer to air.
        const StateID targetBlockState = (newFluid.kind != FluidKind::None)
            ? stateId
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
            sc->setFluidLayer(localX, localY, localZ, stateId);
        } else {
            // Block doesn't allow fluid coexistence — replace the block with fluid
            setBlockState(x, y, z, stateId);
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
}

void World::setThreadPool(ThreadPool* pool) {
    m_threadPool = pool;
    if (m_lightService) {
        m_lightService->shutdown();
        m_lightService->start(m_threadPool);
    }
}

LightFrameStats World::getLightFrameStats() const {
    if (!m_lightService) {
        return {};
    }
    return m_lightService->getFrameStats();
}

RayHit World::raycast(const PhysicsInfo& ray, const float maxDist) const {
    RayHit hitResult{};

    const glm::vec3 rayDir = glm::normalize(ray.direction);
    const glm::vec3 rayOri = ray.origin;

    int x = static_cast<int>(std::floor(rayOri.x));
    int y = static_cast<int>(std::floor(rayOri.y));
    int z = static_cast<int>(std::floor(rayOri.z));

    const int stepX = (rayDir.x > 0.0f) ? 1 : -1;
    const int stepY = (rayDir.y > 0.0f) ? 1 : -1;
    const int stepZ = (rayDir.z > 0.0f) ? 1 : -1;

    const float tDeltaX = (rayDir.x != 0.0f) ? std::abs(1.0f / rayDir.x) : 1e30f;
    const float tDeltaY = (rayDir.y != 0.0f) ? std::abs(1.0f / rayDir.y) : 1e30f;
    const float tDeltaZ = (rayDir.z != 0.0f) ? std::abs(1.0f / rayDir.z) : 1e30f;

    float tMaxX = (stepX > 0) ? (x + 1.0f - rayOri.x) * tDeltaX : (rayOri.x - x) * tDeltaX;
    float tMaxY = (stepY > 0) ? (y + 1.0f - rayOri.y) * tDeltaY : (rayOri.y - y) * tDeltaY;
    float tMaxZ = (stepZ > 0) ? (z + 1.0f - rayOri.z) * tDeltaZ : (rayOri.z - z) * tDeltaZ;

    float dist = 0.0f;
    glm::ivec3 hitNormal(0);

    while (dist <= maxDist) {
        const BlockID block = getBlock(x, y, z);
        if (block != BlockIds::AIR && !FluidState::isWater(block)) {
            const StateID stateId = getBlockState(x, y, z);
            const BlockSelectionBox selectionBox = BlockSelection::getBox(stateId);
            const glm::vec3 boxMin = glm::vec3(x, y, z) + selectionBox.min;
            const glm::vec3 boxMax = glm::vec3(x, y, z) + selectionBox.max;

            float aabbDistance = 0.0f;
            glm::ivec3 aabbNormal(0);
            if (rayIntersectsAabb(rayOri, rayDir, boxMin, boxMax, maxDist, aabbDistance, aabbNormal)) {
                hitResult.hit = true;
                hitResult.blockPos = glm::ivec3(x, y, z);
                hitResult.normal = (aabbNormal.x != 0 || aabbNormal.y != 0 || aabbNormal.z != 0) ? aabbNormal : hitNormal;
                hitResult.distance = aabbDistance;
                return hitResult;
            }
        }

        if (tMaxX < tMaxY) {
            if (tMaxX < tMaxZ) {
                x += stepX;
                dist = tMaxX;
                tMaxX += tDeltaX;
                hitNormal = glm::ivec3(-stepX, 0, 0);
            } else {
                z += stepZ;
                dist = tMaxZ;
                tMaxZ += tDeltaZ;
                hitNormal = glm::ivec3(0, 0, -stepZ);
            }
        } else {
            if (tMaxY < tMaxZ) {
                y += stepY;
                dist = tMaxY;
                tMaxY += tDeltaY;
                hitNormal = glm::ivec3(0, -stepY, 0);
            } else {
                z += stepZ;
                dist = tMaxZ;
                tMaxZ += tDeltaZ;
                hitNormal = glm::ivec3(0, 0, -stepZ);
            }
        }
    }

    return hitResult;
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
    if (!m_threadPool) {
        // No thread pool — fall back to synchronous load
        loadChunk(cx, cz);
        return;
    }

    m_generationInFlight.insert(key);

    auto chunk = std::make_shared<Chunk>(cx, cz);
    TerrainGenerator* terrainGen = &m_terrainGen;

    m_threadPool->submit([chunk, terrainGen, this]() {
        terrainGen->generateChunk(*chunk);
        chunk->seedInitialLightMap();

        {
            std::lock_guard<std::mutex> lock(m_completedGenMutex);
            m_completedGenQueue.push_back(chunk);
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
            if (cur->getSubChunk(scy) || neighbor.getSubChunk(scy)) {
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

    auto chunk = std::make_shared<Chunk>(cx, cz);

    m_terrainGen.generateChunk(*chunk);
    chunk->seedInitialLightMap();

    m_chunks[key] = std::move(chunk);
    ++m_activeChunkRevision;

    // Wire up neighbor pointers for the new chunk and its existing neighbors
    Chunk* cur = m_chunks[key].get();
    auto markNeighborBorderDirty = [&](Chunk& neighbor) {
        for (int scy = 0; scy < Chunk::NUM_SUB_CHUNKS; ++scy) {
            if (cur->getSubChunk(scy) || neighbor.getSubChunk(scy)) {
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
    auto it = m_chunks.find(chunkKey(cx, cz));
    if (it == m_chunks.end()) return;

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
            if (chunk->getSubChunk(scy) || neighbor->getSubChunk(scy)) {
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
