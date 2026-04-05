#include "World.h"
#include <algorithm>
#include <cmath>

namespace {
int worldToChunkCoord(const int world, const int chunkSize) {
    // floor-divide for negative coordinates
    return static_cast<int>(std::floor(static_cast<float>(world) / static_cast<float>(chunkSize)));
}
}
void World::init(uint32_t seed) {
    m_seed = seed;
    m_terrainGen.init(seed, m_flatSurfaceY);
    m_chunks.clear();
    m_loadQueue.clear();
}

void World::update(const glm::vec3& playerPos) {
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
        m_chunks.erase(key);
    }

    // Load up to 4 chunks per frame to avoid severe freezing but keep load speed reasonable
    int loaded = 0;
    while (!m_loadQueue.empty() && loaded < 4) {
        auto pos = m_loadQueue.back();
        m_loadQueue.pop_back();
        loadChunk(pos.x, pos.y);
        loaded++;
    }
}

BlockID World::getBlock(int x, int y, int z) const {
    if (y < 0 || y >= Chunk::SIZE_Y) return BlockType::AIR;

    const int chunkX = worldToChunkCoord(x, Chunk::SIZE_X);
    const int chunkZ = worldToChunkCoord(z, Chunk::SIZE_Z);

    auto it = m_chunks.find(chunkKey(chunkX, chunkZ));
    if (it != m_chunks.end()) {
        int localX = x - chunkX * Chunk::SIZE_X;
        int localZ = z - chunkZ * Chunk::SIZE_Z;
        return it->second->getBlock(localX, y, localZ);
    }
    return BlockType::AIR;
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

    chunk.setBlock(localX, y, localZ, id);

    // Recompute simple sky light for this vertical column so newly exposed blocks light up.
    bool lightChanged = false;
    bool skylightVisible = true;
    for (int scanY = Chunk::SIZE_Y - 1; scanY >= 0; --scanY) {
        const BlockID columnId = chunk.getBlock(localX, scanY, localZ);
        const uint8_t targetSun = skylightVisible ? 15 : 0;
        if (chunk.getSunlight(localX, scanY, localZ) != targetSun) {
            chunk.setSunlight(localX, scanY, localZ, targetSun);
            lightChanged = true;
        }
        if (BlockRegistry::get(columnId).isSolid) {
            skylightVisible = false;
        }
    }

    if (lightChanged) {
        chunk.markDirty();
    }
}

bool World::raycast(const PhysicsInfo& ray, float maxDist, glm::ivec3& hitBlock, glm::ivec3& placeBlock) const {
    glm::vec3 rayDir = glm::normalize(ray.direction);
    glm::vec3 rayOri = ray.origin;

    int x = std::floor(rayOri.x);
    int y = std::floor(rayOri.y);
    int z = std::floor(rayOri.z);

    float stepX = (rayDir.x > 0) ? 1.0f : -1.0f;
    float stepY = (rayDir.y > 0) ? 1.0f : -1.0f;
    float stepZ = (rayDir.z > 0) ? 1.0f : -1.0f;

    float tDeltaX = (rayDir.x != 0.0f) ? std::abs(1.0f / rayDir.x) : 1e30f;
    float tDeltaY = (rayDir.y != 0.0f) ? std::abs(1.0f / rayDir.y) : 1e30f;
    float tDeltaZ = (rayDir.z != 0.0f) ? std::abs(1.0f / rayDir.z) : 1e30f;

    float tMaxX = (stepX > 0) ? (x + 1.0f - rayOri.x) * tDeltaX : (rayOri.x - x) * tDeltaX;
    float tMaxY = (stepY > 0) ? (y + 1.0f - rayOri.y) * tDeltaY : (rayOri.y - y) * tDeltaY;
    float tMaxZ = (stepZ > 0) ? (z + 1.0f - rayOri.z) * tDeltaZ : (rayOri.z - z) * tDeltaZ;

    int lastX = x, lastY = y, lastZ = z;

    float dist = 0.0f;
    while (dist <= maxDist) {
        BlockID block = getBlock(x, y, z);
        if (block != BlockType::AIR) {
            hitBlock = glm::ivec3(x, y, z);
            placeBlock = glm::ivec3(lastX, lastY, lastZ);
            return true;
        }

        lastX = x;
        lastY = y;
        lastZ = z;

        if (tMaxX < tMaxY) {
            if (tMaxX < tMaxZ) {
                x += static_cast<int>(stepX);
                dist = tMaxX;
                tMaxX += tDeltaX;
            } else {
                z += static_cast<int>(stepZ);
                dist = tMaxZ;
                tMaxZ += tDeltaZ;
            }
        } else {
            if (tMaxY < tMaxZ) {
                y += static_cast<int>(stepY);
                dist = tMaxY;
                tMaxY += tDeltaY;
            } else {
                z += static_cast<int>(stepZ);
                dist = tMaxZ;
                tMaxZ += tDeltaZ;
            }
        }
    }
    return false;
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
            if (it->second->getBlock(localX, y, localZ) != BlockType::AIR) {
                return y;
            }
        }
        return 0;
    }

    return m_terrainGen.sampleSurfaceY(x, z);
}

int64_t World::chunkKey(int cx, int cz) {
    return (static_cast<int64_t>(cx) << 32) | (static_cast<int64_t>(cz) & 0xFFFFFFFF);
}

void World::loadChunk(int cx, int cz) {
    int64_t key = chunkKey(cx, cz);
    if (m_chunks.find(key) != m_chunks.end()) return;

    auto chunk = std::make_unique<Chunk>(cx, cz);

    m_terrainGen.generateChunk(*chunk);

    m_chunks[key] = std::move(chunk);
}

void World::unloadChunk(int cx, int cz) {
    m_chunks.erase(chunkKey(cx, cz));
}

size_t World::getTotalVertexCount() const {
    size_t total = 0;
    for (const auto& pair : m_chunks) {
        if (pair.second) {
            total += pair.second->getMesh().vertexCount;
            total += pair.second->getMesh().transparentVertexCount;
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
