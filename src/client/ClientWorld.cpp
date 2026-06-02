#include "ClientWorld.h"
#include "../world/block/Block.h"
#include <cmath>

namespace client {
namespace {

void markRenderableBorderDirty(Chunk& chunk) {
    for (int scy = 0; scy < Chunk::NUM_SUB_CHUNKS; ++scy) {
        if (chunk.getSubChunk(scy) != nullptr) {
            chunk.markSubChunkDirty(scy);
        }
    }
}

} // namespace

ClientWorld::ClientWorld() = default;
ClientWorld::~ClientWorld() = default;

const ClientWorld::ChunkMap& ClientWorld::getActiveChunks() const {
    // In the in-process model, the server tick completes before rendering,
    // so no concurrent modification occurs during iteration.
    // For Phase 6 (real networking), double-buffering or snapshot copies will be needed.
    return m_chunks;
}

uint64_t ClientWorld::getActiveChunkRevision() const {
    return m_activeChunkRevision;
}

BlockID ClientWorld::getBlock(int x, int y, int z) const {
    if (y < 0 || y >= 256) return BlockIds::AIR;
    const int cx = static_cast<int>(std::floor(static_cast<float>(x) / 16.0f));
    const int cz = static_cast<int>(std::floor(static_cast<float>(z) / 16.0f));
    const int64_t key = chunkKey(cx, cz);

    std::lock_guard lock(m_chunksMutex);
    auto it = m_chunks.find(key);
    if (it == m_chunks.end() || !it->second) return BlockIds::AIR;
    const int lx = x - cx * 16;
    const int lz = z - cz * 16;
    return it->second->getBlock(lx, y, lz);
}

uint8_t ClientWorld::getPackedLight(int x, int y, int z) const {
    if (y < 0 || y >= 256) return 0;
    const int cx = static_cast<int>(std::floor(static_cast<float>(x) / 16.0f));
    const int cz = static_cast<int>(std::floor(static_cast<float>(z) / 16.0f));
    const int64_t key = chunkKey(cx, cz);

    std::lock_guard lock(m_chunksMutex);
    auto it = m_chunks.find(key);
    if (it == m_chunks.end() || !it->second) return 0;
    const int lx = x - cx * 16;
    const int lz = z - cz * 16;
    return it->second->getPackedLight(lx, y, lz);
}

StateID ClientWorld::getBlockState(int x, int y, int z) const {
    // BlockID and StateID are both uint16_t; getBlock serves as getBlockState.
    return static_cast<StateID>(getBlock(x, y, z));
}

StateID ClientWorld::getFluidState(int x, int y, int z) const {
    if (y < 0 || y >= 256) return StateID{0};
    const int cx = static_cast<int>(std::floor(static_cast<float>(x) / 16.0f));
    const int cz = static_cast<int>(std::floor(static_cast<float>(z) / 16.0f));
    const int64_t key = chunkKey(cx, cz);

    std::lock_guard lock(m_chunksMutex);
    auto it = m_chunks.find(key);
    if (it == m_chunks.end() || !it->second) return StateID{0};
    const int lx = x - cx * 16;
    const int lz = z - cz * 16;
    return it->second->getFluidState(lx, y, lz);
}

bool ClientWorld::isChunkLoadedForBlock(int x, int y, int z) const {
    (void)y;
    const int cx = static_cast<int>(std::floor(static_cast<float>(x) / 16.0f));
    const int cz = static_cast<int>(std::floor(static_cast<float>(z) / 16.0f));
    const int64_t key = chunkKey(cx, cz);

    std::lock_guard lock(m_chunksMutex);
    return m_chunks.count(key) > 0;
}

int ClientWorld::getRenderDistance() const {
    return m_renderDistance;
}

glm::ivec2 ClientWorld::getChunkCoords(int worldX, int worldZ) const {
    return {
        static_cast<int>(std::floor(static_cast<float>(worldX) / 16.0f)),
        static_cast<int>(std::floor(static_cast<float>(worldZ) / 16.0f))
    };
}

TerrainBiome ClientWorld::getBiome(int x, int z) const {
    (void)x;
    (void)z;
    // Biome data is computed by the server's TerrainGenerator and not stored in chunks.
    // For the in-process milestone, the renderer uses the server's World directly for biome queries.
    // This fallback returns Temperate for any ClientWorld-only path.
    // In Phase 2+, biome data will be included in chunk data messages from the server.
    return TerrainBiome::Temperate;
}

void ClientWorld::addChunk(std::shared_ptr<Chunk> chunk) {
    if (!chunk) return;
    const int cx = chunk->m_chunkX;
    const int cz = chunk->m_chunkZ;
    const int64_t key = chunkKey(chunk->m_chunkX, chunk->m_chunkZ);
    std::lock_guard lock(m_chunksMutex);

    auto linkNeighbor = [&](const int dx, const int dz, const int selfDir, const int neighborDir) {
        const int64_t neighborKey = chunkKey(cx + dx, cz + dz);
        auto it = m_chunks.find(neighborKey);
        if (it == m_chunks.end() || !it->second) {
            return;
        }
        chunk->neighbors[selfDir] = it->second.get();
        it->second->neighbors[neighborDir] = chunk.get();
        chunk->linkExistingSubChunksWithNeighbor(selfDir);
        it->second->linkExistingSubChunksWithNeighbor(neighborDir);
        markRenderableBorderDirty(*chunk);
        markRenderableBorderDirty(*it->second);
    };

    linkNeighbor(1, 0, 0, 1);
    linkNeighbor(-1, 0, 1, 0);
    linkNeighbor(0, 1, 2, 3);
    linkNeighbor(0, -1, 3, 2);

    m_chunks[key] = std::move(chunk);
    ++m_activeChunkRevision;
}

void ClientWorld::removeChunk(int cx, int cz) {
    const int64_t key = chunkKey(cx, cz);
    std::lock_guard lock(m_chunksMutex);
    auto it = m_chunks.find(key);
    if (it != m_chunks.end() && it->second) {
        Chunk& chunk = *it->second;
        for (int dir = 0; dir < 4; ++dir) {
            if (Chunk* neighbor = chunk.neighbors[dir]) {
                const int opposite = (dir == 0) ? 1 : (dir == 1) ? 0 : (dir == 2) ? 3 : 2;
                chunk.unlinkExistingSubChunksFromNeighbor(dir);
                neighbor->neighbors[opposite] = nullptr;
                markRenderableBorderDirty(*neighbor);
            }
            chunk.neighbors[dir] = nullptr;
        }
    }
    if (m_chunks.erase(key) > 0) {
        ++m_activeChunkRevision;
    }
}

void ClientWorld::applyBlockUpdate(int x, int y, int z, BlockID blockId) {
    if (y < 0 || y >= 256) return;
    const int cx = static_cast<int>(std::floor(static_cast<float>(x) / 16.0f));
    const int cz = static_cast<int>(std::floor(static_cast<float>(z) / 16.0f));
    const int64_t key = chunkKey(cx, cz);

    std::lock_guard lock(m_chunksMutex);
    auto it = m_chunks.find(key);
    if (it == m_chunks.end() || !it->second) return;
    const int lx = x - cx * 16;
    const int lz = z - cz * 16;
    Chunk& chunk = *it->second;
    chunk.setBlock(lx, y, lz, blockId);
    if (lx == 0 && chunk.neighbors[1]) {
        chunk.neighbors[1]->markSubChunkDirty(Chunk::toSubChunkIndex(y));
    } else if (lx == Chunk::SIZE_X - 1 && chunk.neighbors[0]) {
        chunk.neighbors[0]->markSubChunkDirty(Chunk::toSubChunkIndex(y));
    }
    if (lz == 0 && chunk.neighbors[3]) {
        chunk.neighbors[3]->markSubChunkDirty(Chunk::toSubChunkIndex(y));
    } else if (lz == Chunk::SIZE_Z - 1 && chunk.neighbors[2]) {
        chunk.neighbors[2]->markSubChunkDirty(Chunk::toSubChunkIndex(y));
    }
}

void ClientWorld::setRenderDistance(int distance) {
    m_renderDistance = distance;
}

size_t ClientWorld::loadedChunkCount() const {
    std::lock_guard lock(m_chunksMutex);
    return m_chunks.size();
}

} // namespace client
