#include "World.h"
#include <algorithm>
#include <cmath>
#include "../core/Time.h"

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
}
void World::init(uint32_t seed) {
    m_seed = seed;
    m_terrainGen.init(seed, m_flatSurfaceY);
    m_chunks.clear();
    m_loadQueue.clear();
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
        if (std::abs(cx - playerChunkX) > m_renderDistance || std::abs(cz - playerChunkZ) > m_renderDistance) {
            toUnload.push_back(pair.first);
        }
    }
    for (int64_t key : toUnload) {
        const int cx = static_cast<int>(key >> 32);
        const int cz = static_cast<int>(key & 0xFFFFFFFF);
        unloadChunk(cx, cz);
    }

    // Keep per-frame chunk creation conservative to reduce visible frame spikes.
    constexpr int kMaxChunkLoadsPerFrame = 2;
    int loaded = 0;
    while (!m_loadQueue.empty() && loaded < kMaxChunkLoadsPerFrame) {

        auto pos = m_loadQueue.back();
        m_loadQueue.pop_back();
        loadChunk(pos.x, pos.y);
        loaded++;
    }

    if (m_lightService) {
        m_lightService->submitJobs(playerPos, 8);
        m_lightService->drainCompleted(*this, 32);
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

    const BlockID oldId = chunk.getBlock(localX, y, localZ);
    if (oldId == id) {
        return;
    }

    if (m_lightService) {
        chunk.setBlockWithoutMeshDirty(localX, y, localZ, id);
        m_lightService->onBlockChanged(x, y, z, oldId, id);
    } else {
        chunk.setBlock(localX, y, localZ, id);
    }


    // Geometry edits must always trigger remesh, regardless of lighting pipeline.
    const int editedScy = Chunk::toSubChunkIndex(y);
    const int localY = Chunk::toSubChunkLocalY(y);
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
        if (block != BlockIds::AIR && block != BlockIds::WATER) {
            hitResult.hit = true;
            hitResult.blockPos = glm::ivec3(x, y, z);
            hitResult.normal = hitNormal;
            hitResult.distance = dist;
            return hitResult;
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
            int cx = playerChunkX + dx;
            int cz = playerChunkZ + dz;
            if (m_chunks.find(chunkKey(cx, cz)) == m_chunks.end()) {
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
