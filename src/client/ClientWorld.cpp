#include "ClientWorld.h"
#include "../net/Protocol.h"
#include "../world/block/Block.h"
#include "../world/fluid/FluidState.h"
#include <cstddef>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace client {
namespace {
[[noreturn]] void failClientWorld(const std::string& message) {
    std::cerr << message << '\n';
    std::abort();
}

void markRenderableBorderDirty(Chunk& chunk) {
    for (int scy = 0; scy < Chunk::NUM_SUB_CHUNKS; ++scy) {
        if (chunk.getSubChunk(scy) != nullptr) {
            chunk.markSubChunkDirty(scy);
        }
    }
}

int inferOddCubeSide(const std::size_t valueCount) {
    for (int side = 1; side <= Chunk::SIZE_Y; side += 2) {
        const std::size_t sideSize = static_cast<std::size_t>(side);
        const std::size_t cubeSize = sideSize * sideSize * sideSize;
        if (cubeSize == valueCount) {
            return side;
        }
        if (cubeSize > valueCount) {
            break;
        }
    }
    return 0;
}

} // namespace

ClientWorld::ClientWorld() = default;
ClientWorld::~ClientWorld() = default;

const ClientWorld::ChunkMap& ClientWorld::getActiveChunks() const {
    std::lock_guard lock(m_chunksMutex);
    if (m_activeChunksSnapshotRevision != m_activeChunkRevision) {
        m_activeChunksSnapshot = m_chunks;
        m_activeChunksSnapshotRevision = m_activeChunkRevision;
    }
    return m_activeChunksSnapshot;
}

uint64_t ClientWorld::getActiveChunkRevision() const {
    std::lock_guard lock(m_chunksMutex);
    return m_activeChunkRevision;
}

uint64_t ClientWorld::getBlockContentRevision() const {
    std::lock_guard lock(m_chunksMutex);
    return m_blockContentRevision;
}

BlockStateId ClientWorld::getBlock(int x, int y, int z) const {
    if (y < 0 || y >= 256)
        return NULL_BLOCK_STATE;
    const int cx = static_cast<int>(std::floor(static_cast<float>(x) / 16.0f));
    const int cz = static_cast<int>(std::floor(static_cast<float>(z) / 16.0f));
    const int64_t key = chunkKey(cx, cz);

    std::lock_guard lock(m_chunksMutex);
    auto it = m_chunks.find(key);
    if (it == m_chunks.end() || !it->second)
        return NULL_BLOCK_STATE;
    const int lx = x - cx * 16;
    const int lz = z - cz * 16;
    return it->second->getBlock(lx, y, lz);
}

uint8_t ClientWorld::getPackedLight(int x, int y, int z) const {
    if (y < 0 || y >= 256)
        return 0;
    const int cx = static_cast<int>(std::floor(static_cast<float>(x) / 16.0f));
    const int cz = static_cast<int>(std::floor(static_cast<float>(z) / 16.0f));
    const int64_t key = chunkKey(cx, cz);

    std::lock_guard lock(m_chunksMutex);
    auto it = m_chunks.find(key);
    if (it == m_chunks.end() || !it->second)
        return 0;
    const int lx = x - cx * 16;
    const int lz = z - cz * 16;
    return it->second->getPackedLight(lx, y, lz);
}

BlockStateId ClientWorld::getBlockState(int x, int y, int z) const {
    return getBlock(x, y, z);
}

BlockStateId ClientWorld::getFluidState(int x, int y, int z) const {
    if (y < 0 || y >= 256)
        return NULL_BLOCK_STATE;
    const int cx = static_cast<int>(std::floor(static_cast<float>(x) / 16.0f));
    const int cz = static_cast<int>(std::floor(static_cast<float>(z) / 16.0f));
    const int64_t key = chunkKey(cx, cz);

    std::lock_guard lock(m_chunksMutex);
    auto it = m_chunks.find(key);
    if (it == m_chunks.end() || !it->second)
        return NULL_BLOCK_STATE;
    const int lx = x - cx * 16;
    const int lz = z - cz * 16;
    const BlockStateId fluidState = it->second->getFluidState(lx, y, lz);
    if (fluidState != NULL_BLOCK_STATE) {
        return fluidState;
    }
    return FluidState::getFluidState(it->second->getBlock(lx, y, lz));
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
    return {static_cast<int>(std::floor(static_cast<float>(worldX) / 16.0f)),
            static_cast<int>(std::floor(static_cast<float>(worldZ) / 16.0f))};
}

TerrainBiome ClientWorld::getBiome(int x, int z) const {
    (void)x;
    (void)z;
    // Biome data is computed by the server's TerrainGenerator and not stored in chunks.
    // For the in-process milestone, the renderer uses the server's World directly for biome queries.
    // ClientWorld exposes a fixed biome until server chunk messages carry biome samples.
    // In Phase 2+, biome data will be included in chunk data messages from the server.
    return TerrainBiome::Temperate;
}

bool ClientWorld::copyWireContainerParts(const glm::ivec3& position, WireContainerParts& out) const {
    std::lock_guard lock(m_chunksMutex);
    const WireContainerParts* parts = m_wireContainerParts.find(position);
    if (parts == nullptr) {
        return false;
    }
    out = *parts;
    return true;
}

void ClientWorld::addChunk(std::shared_ptr<Chunk> chunk) {
    if (!chunk)
        return;
    const int cx = chunk->m_chunkX;
    const int cz = chunk->m_chunkZ;
    const int64_t key = chunkKey(chunk->m_chunkX, chunk->m_chunkZ);
    std::lock_guard lock(m_chunksMutex);

    auto existingIt = m_chunks.find(key);
    if (existingIt != m_chunks.end() && existingIt->second) {
        Chunk& existing = *existingIt->second;
        for (int dir = 0; dir < 4; ++dir) {
            if (Chunk* neighbor = existing.neighbors[dir]) {
                const int opposite = (dir == 0) ? 1 : (dir == 1) ? 0 : (dir == 2) ? 3 : 2;
                existing.unlinkExistingSubChunksFromNeighbor(dir);
                neighbor->neighbors[opposite] = nullptr;
                markRenderableBorderDirty(*neighbor);
            }
            existing.neighbors[dir] = nullptr;
        }
    }

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
    ++m_blockContentRevision;
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
        ++m_blockContentRevision;
    }
}

void ClientWorld::applyBlockUpdate(int x, int y, int z, BlockStateId stateId) {
    static const std::vector<uint8_t> kNoLightPatch;
    applyBlockUpdate(x, y, z, stateId, kNoLightPatch);
}

void ClientWorld::applyBlockUpdate(int x, int y, int z, BlockStateId stateId,
                                   const std::vector<uint8_t>& packedLightPatch) {
    applyBlockUpdate(x, y, z, net::BlockUpdateKind::BlockState, stateId, packedLightPatch);
}

void ClientWorld::applyBlockUpdate(const int x, const int y, const int z, const net::BlockUpdateKind kind,
                                   const BlockStateId stateId, const std::vector<uint8_t>& packedLightPatch) {
    if (y < 0 || y >= 256)
        return;
    const int cx = static_cast<int>(std::floor(static_cast<float>(x) / 16.0f));
    const int cz = static_cast<int>(std::floor(static_cast<float>(z) / 16.0f));
    const int64_t key = chunkKey(cx, cz);

    std::lock_guard lock(m_chunksMutex);
    auto it = m_chunks.find(key);
    if (it == m_chunks.end() || !it->second)
        return;
    const int lx = x - cx * 16;
    const int lz = z - cz * 16;
    Chunk& chunk = *it->second;
    if (kind == net::BlockUpdateKind::BlockState) {
        chunk.setBlock(lx, y, lz, stateId);
        const BlockID blockId = stateId == NULL_BLOCK_STATE ? RUNTIME_ID_NULL : BlockStateRegistry::getBlockId(stateId);
        if (blockId == RUNTIME_ID_NULL || !BlockRegistry::getFast(blockId).isWireContainer) {
            m_wireContainerParts.erase(glm::ivec3(x, y, z));
        }
        ++m_blockContentRevision;
        chunk.recalcHeightMap(lx, lz);
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

    if (!packedLightPatch.empty()) {
        if (packedLightPatch.size() == Chunk::BLOCK_COUNT) {
            chunk.replacePackedLight(packedLightPatch.data(), packedLightPatch.size());
        } else if (packedLightPatch.size() == SubChunk::BLOCK_COUNT) {
            chunk.replacePackedLightSection(Chunk::toSubChunkIndex(y), packedLightPatch.data(),
                                            packedLightPatch.size());
        } else if (const int patchSide = inferOddCubeSide(packedLightPatch.size()); patchSide > 0) {
            const int patchRadius = patchSide / 2;
            size_t index = 0;
            for (int dy = -patchRadius; dy <= patchRadius; ++dy) {
                for (int dz = -patchRadius; dz <= patchRadius; ++dz) {
                    for (int dx = -patchRadius; dx <= patchRadius; ++dx) {
                        const int wx = x + dx;
                        const int wy = y + dy;
                        const int wz = z + dz;
                        const uint8_t packed = packedLightPatch[index++];
                        if (wy < 0 || wy >= Chunk::SIZE_Y) {
                            continue;
                        }
                        const int pcx = static_cast<int>(std::floor(static_cast<float>(wx) / 16.0f));
                        const int pcz = static_cast<int>(std::floor(static_cast<float>(wz) / 16.0f));
                        auto pit = m_chunks.find(chunkKey(pcx, pcz));
                        if (pit == m_chunks.end() || !pit->second) {
                            continue;
                        }
                        const int plx = wx - pcx * Chunk::SIZE_X;
                        const int plz = wz - pcz * Chunk::SIZE_Z;
                        pit->second->setSunlight(plx, wy, plz, static_cast<uint8_t>((packed >> 4) & 0x0F));
                        pit->second->setBlockLight(plx, wy, plz, static_cast<uint8_t>(packed & 0x0F));
                        pit->second->markSubChunkDirty(Chunk::toSubChunkIndex(wy));
                    }
                }
            }
        }
    }
}

void ClientWorld::applyWireContainerUpdate(const glm::ivec3& position, const WireContainerParts& parts) {
    if (position.y < 0 || position.y >= 256) {
        return;
    }

    const int cx = static_cast<int>(std::floor(static_cast<float>(position.x) / 16.0f));
    const int cz = static_cast<int>(std::floor(static_cast<float>(position.z) / 16.0f));
    const int64_t key = chunkKey(cx, cz);

    std::lock_guard lock(m_chunksMutex);
    auto it = m_chunks.find(key);
    if (it == m_chunks.end() || !it->second) {
        return;
    }

    const int lx = position.x - cx * 16;
    const int lz = position.z - cz * 16;
    const BlockStateId stateId = it->second->getBlock(lx, position.y, lz);
    if (stateId == NULL_BLOCK_STATE ||
        !BlockRegistry::getFast(BlockStateRegistry::getBlockId(stateId)).isWireContainer) {
        failClientWorld("Client wire container update targeted a non-container block");
    }

    m_wireContainerParts.getOrCreate(position) = parts;
    ++m_blockContentRevision;
    it->second->markSubChunkDirty(Chunk::toSubChunkIndex(position.y));
}

void ClientWorld::eraseWireContainerParts(const glm::ivec3& position) {
    std::lock_guard lock(m_chunksMutex);
    m_wireContainerParts.erase(position);
    ++m_blockContentRevision;
}

void ClientWorld::setRenderDistance(int distance) {
    std::lock_guard lock(m_chunksMutex);
    m_renderDistance = distance;
}

size_t ClientWorld::loadedChunkCount() const {
    std::lock_guard lock(m_chunksMutex);
    return m_chunks.size();
}

ClientWorld::ChunkLoadProgress ClientWorld::getChunkLoadProgress(const glm::vec3& center) const {
    const int centerChunkX = static_cast<int>(std::floor(center.x / static_cast<float>(Chunk::SIZE_X)));
    const int centerChunkZ = static_cast<int>(std::floor(center.z / static_cast<float>(Chunk::SIZE_Z)));
    const int loadRadius = m_renderDistance + 1;

    ChunkLoadProgress progress{};
    std::lock_guard lock(m_chunksMutex);
    for (int dx = -loadRadius; dx <= loadRadius; ++dx) {
        for (int dz = -loadRadius; dz <= loadRadius; ++dz) {
            if (dx * dx + dz * dz > loadRadius * loadRadius) {
                continue;
            }

            ++progress.target;
            const int64_t key = chunkKey(centerChunkX + dx, centerChunkZ + dz);
            if (m_chunks.find(key) != m_chunks.end()) {
                ++progress.loaded;
            }
        }
    }
    return progress;
}

} // namespace client
