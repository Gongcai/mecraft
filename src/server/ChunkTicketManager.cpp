#include "ChunkTicketManager.h"
#include <algorithm>
#include <cmath>

ChunkTicketManager::ChunkTicketManager() = default;

void ChunkTicketManager::reset() {
    m_playerChunk = glm::ivec2(0);
}

void ChunkTicketManager::updatePlayerPosition(int chunkX, int chunkZ) {
    m_playerChunk = glm::ivec2(chunkX, chunkZ);
}

void ChunkTicketManager::addSpawnTicket(int chunkX, int chunkZ) {
    // Spawn tickets are handled by checking distance to (0,0) in shouldLoad/shouldTick
    // For now, spawn is at origin — the simulation center handles this.
    (void)chunkX;
    (void)chunkZ;
}

bool ChunkTicketManager::shouldTick(int cx, int cz) const {
    return isWithinRadius(cx, cz, m_playerChunk.x, m_playerChunk.y, m_simulationRadius);
}

bool ChunkTicketManager::shouldLoad(int cx, int cz) const {
    return isWithinRadius(cx, cz, m_playerChunk.x, m_playerChunk.y, loadRadius());
}

bool ChunkTicketManager::shouldUnload(int cx, int cz) const {
    // Unload only if outside the hysteresis band (unloadRadius > loadRadius)
    return !isWithinRadius(cx, cz, m_playerChunk.x, m_playerChunk.y, unloadRadius());
}

std::vector<glm::ivec2> ChunkTicketManager::getChunksToLoad(
    int maxCount,
    const std::unordered_set<int64_t>& alreadyLoaded) const {

    const int radius = loadRadius();
    const int centerX = m_playerChunk.x;
    const int centerZ = m_playerChunk.y;

    // Collect candidate chunks within load radius
    std::vector<glm::ivec2> candidates;
    candidates.reserve(static_cast<size_t>(2 * radius + 1) * (2 * radius + 1));

    for (int dx = -radius; dx <= radius; ++dx) {
        for (int dz = -radius; dz <= radius; ++dz) {
            const int cx = centerX + dx;
            const int cz = centerZ + dz;
            if (!isWithinRadius(cx, cz, centerX, centerZ, radius)) {
                continue;
            }
            const int64_t key = chunkKey(cx, cz);
            if (alreadyLoaded.count(key) == 0) {
                candidates.push_back({cx, cz});
            }
        }
    }

    // Sort by distance to center (closest first)
    std::sort(candidates.begin(), candidates.end(),
              [centerX, centerZ](const glm::ivec2& a, const glm::ivec2& b) {
                  const int dA = distanceSq(a.x, a.y, centerX, centerZ);
                  const int dB = distanceSq(b.x, b.y, centerX, centerZ);
                  return dA < dB;
              });

    // Truncate to maxCount
    if (static_cast<int>(candidates.size()) > maxCount) {
        candidates.resize(static_cast<size_t>(maxCount));
    }

    return candidates;
}

std::vector<int64_t> ChunkTicketManager::getChunksToUnload(
    const std::unordered_set<int64_t>& loadedChunks) const {

    std::vector<int64_t> toUnload;
    for (const int64_t key : loadedChunks) {
        const int cx = static_cast<int>(key >> 32);
        const int cz = static_cast<int>(static_cast<int32_t>(key & 0xFFFFFFFF));
        if (shouldUnload(cx, cz)) {
            toUnload.push_back(key);
        }
    }
    return toUnload;
}

int64_t ChunkTicketManager::chunkKey(int cx, int cz) {
    return (static_cast<int64_t>(cx) << 32) | static_cast<int64_t>(static_cast<uint32_t>(cz));
}

bool ChunkTicketManager::isWithinRadius(int cx, int cz, int centerX, int centerZ, int radius) {
    const int dx = cx - centerX;
    const int dz = cz - centerZ;
    return dx * dx + dz * dz <= radius * radius;
}

int ChunkTicketManager::distanceSq(int cx, int cz, int centerX, int centerZ) {
    const int dx = cx - centerX;
    const int dz = cz - centerZ;
    return dx * dx + dz * dz;
}
