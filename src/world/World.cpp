#include "World.h"
#include "LightEngine.h"
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
    m_chunks.clear();
    m_loadQueue.clear();
}

void World::shutdown() {
    if (m_isShutdown) {
        return;
    }
    m_isShutdown = true;

    // Clear all neighbor pointers before destroying chunks to prevent dangling pointers
    for (auto& pair : m_chunks) {
        Chunk* chunk = pair.second.get();
        if (chunk) {
            chunk->neighbors[0] = nullptr;
            chunk->neighbors[1] = nullptr;
            chunk->neighbors[2] = nullptr;
            chunk->neighbors[3] = nullptr;
        }
    }
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
        int cx = static_cast<int>(key >> 32);
        int cz = static_cast<int>(key & 0xFFFFFFFF);
        unloadChunk(cx, cz);
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

    // Use LightEngine for proper light propagation
    if (m_lightEngine) {
        m_lightEngine->onBlockChange(x, y, z, oldId, id);
    } else {
        // Fallback: simple sky light update
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

int64_t World::chunkKey(int cx, int cz) {
    return (static_cast<int64_t>(cx) << 32) | (static_cast<int64_t>(cz) & 0xFFFFFFFF);
}

void World::loadChunk(int cx, int cz) {
    int64_t key = chunkKey(cx, cz);
    if (m_chunks.find(key) != m_chunks.end()) return;

    auto chunk = std::make_unique<Chunk>(cx, cz);

    // Generate terrain
    for (int x = 0; x < Chunk::SIZE_X; ++x) {
        for (int z = 0; z < Chunk::SIZE_Z; ++z) {
            for (int y = 0; y <= m_flatSurfaceY; ++y) {
                if (y == m_flatSurfaceY) {
                    chunk->setBlock(x, y, z, BlockType::GRASS);
                } else if (y >= m_flatSurfaceY - 3) {
                    chunk->setBlock(x, y, z, BlockType::DIRT);
                } else {
                    chunk->setBlock(x, y, z, BlockType::STONE);
                }
            }
        }
    }

    // Initialize lighting using LightEngine
    if (m_lightEngine) {
        m_lightEngine->initChunkLight(*chunk);
    } else {
        // Fallback: simple sky light
        chunk->rebuildHeightmap();
        for (int x = 0; x < Chunk::SIZE_X; ++x) {
            for (int z = 0; z < Chunk::SIZE_Z; ++z) {
                int height = chunk->getHeightmap(x, z);
                for (int y = height + 1; y < Chunk::SIZE_Y; ++y) {
                    chunk->setSunlight(x, y, z, 15);
                }
            }
        }
    }

    // Set up neighbor references
    Chunk* chunkPtr = chunk.get();
    m_chunks[key] = std::move(chunk);

    // Helper lambda to get chunk at chunk coordinates
    auto getChunkAt = [this](int chunkX, int chunkZ) -> Chunk* {
        int64_t key = chunkKey(chunkX, chunkZ);
        auto it = m_chunks.find(key);
        if (it != m_chunks.end()) {
            return it->second.get();
        }
        return nullptr;
    };

    // Link neighbors
    Chunk* posX = getChunkAt(cx + 1, cz);
    Chunk* negX = getChunkAt(cx - 1, cz);
    Chunk* posZ = getChunkAt(cx, cz + 1);
    Chunk* negZ = getChunkAt(cx, cz - 1);

    chunkPtr->neighbors[0] = posX;
    chunkPtr->neighbors[1] = negX;
    chunkPtr->neighbors[2] = posZ;
    chunkPtr->neighbors[3] = negZ;

    // Update neighbor pointers to this chunk
    if (posX) posX->neighbors[1] = chunkPtr;
    if (negX) negX->neighbors[0] = chunkPtr;
    if (posZ) posZ->neighbors[3] = chunkPtr;
    if (negZ) negZ->neighbors[2] = chunkPtr;
}

void World::unloadChunk(int cx, int cz) {
    int64_t key = chunkKey(cx, cz);
    auto it = m_chunks.find(key);
    if (it == m_chunks.end()) return;

    Chunk* chunk = it->second.get();

    // Clear neighbor pointers from adjacent chunks to prevent dangling pointers
    Chunk* posX = chunk->neighbors[0];
    Chunk* negX = chunk->neighbors[1];
    Chunk* posZ = chunk->neighbors[2];
    Chunk* negZ = chunk->neighbors[3];

    if (posX) posX->neighbors[1] = nullptr;
    if (negX) negX->neighbors[0] = nullptr;
    if (posZ) posZ->neighbors[3] = nullptr;
    if (negZ) negZ->neighbors[2] = nullptr;

    m_chunks.erase(key);
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
