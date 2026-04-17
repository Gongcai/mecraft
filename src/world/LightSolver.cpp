#include "LightSolver.h"

#include <algorithm>
#include <chrono>
#include <deque>

#include "Block.h"
#include "Chunk.h"

namespace {
struct LightNode {
    int x = 0;
    int y = 0;
    int z = 0;
};

constexpr int DX[6] = {1, -1, 0, 0, 0, 0};
constexpr int DY[6] = {0, 0, 1, -1, 0, 0};
constexpr int DZ[6] = {0, 0, 0, 0, 1, -1};

inline std::size_t packedIndex(const int x, const int y, const int z) {
    return Chunk::toIndex(x, y, z);
}

inline uint8_t getSky(const std::vector<uint8_t>& packed, const int x, const int y, const int z) {
    return static_cast<uint8_t>((packed[packedIndex(x, y, z)] >> 4) & 0x0F);
}

inline uint8_t getBlock(const std::vector<uint8_t>& packed, const int x, const int y, const int z) {
    return static_cast<uint8_t>(packed[packedIndex(x, y, z)] & 0x0F);
}

inline void setSky(std::vector<uint8_t>& packed, const int x, const int y, const int z, const uint8_t value) {
    const std::size_t idx = packedIndex(x, y, z);
    packed[idx] = static_cast<uint8_t>((packed[idx] & 0x0F) | ((value & 0x0F) << 4));
}

inline void setBlock(std::vector<uint8_t>& packed, const int x, const int y, const int z, const uint8_t value) {
    const std::size_t idx = packedIndex(x, y, z);
    packed[idx] = static_cast<uint8_t>((packed[idx] & 0xF0) | (value & 0x0F));
}

bool isInside(const int x, const int y, const int z) {
    return x >= 0 && x < Chunk::SIZE_X && y >= 0 && y < Chunk::SIZE_Y && z >= 0 && z < Chunk::SIZE_Z;
}

BlockID getBlockId(const LightJob& job, const int x, const int y, const int z) {
    if (!isInside(x, y, z)) {
        return BlockIds::AIR;
    }
    if (job.blockSnapshot.size() == Chunk::BLOCK_COUNT) {
        return job.blockSnapshot[packedIndex(x, y, z)];
    }
    return job.chunk ? job.chunk->getBlock(x, y, z) : BlockIds::AIR;
}

uint8_t getOpacity(const LightJob& job, const int x, const int y, const int z) {
    return BlockRegistry::get(getBlockId(job, x, y, z)).opacity;
}

uint32_t borderFaceDirtyMask(const std::vector<uint8_t>& before,
                             const std::vector<uint8_t>& after,
                             const int direction) {
    if (before.size() != Chunk::BLOCK_COUNT || after.size() != Chunk::BLOCK_COUNT) {
        return 0xFFFFFFFFu;
    }

    uint32_t dirtyMask = 0;
    if (direction == 0 || direction == 1) {
        const int x = (direction == 0) ? (Chunk::SIZE_X - 1) : 0;
        for (int y = 0; y < Chunk::SIZE_Y; ++y) {
            for (int z = 0; z < Chunk::SIZE_Z; ++z) {
                const std::size_t idx = packedIndex(x, y, z);
                if (before[idx] != after[idx]) {
                    dirtyMask |= (1u << Chunk::toSubChunkIndex(y));
                    break;
                }
            }
        }
        return dirtyMask;
    }

    const int z = (direction == 2) ? (Chunk::SIZE_Z - 1) : 0;
    for (int y = 0; y < Chunk::SIZE_Y; ++y) {
        for (int x = 0; x < Chunk::SIZE_X; ++x) {
            const std::size_t idx = packedIndex(x, y, z);
            if (before[idx] != after[idx]) {
                dirtyMask |= (1u << Chunk::toSubChunkIndex(y));
                break;
            }
        }
    }
    return dirtyMask;
}

void emitBoundarySeeds(const LightJob& job,
                       const std::vector<uint8_t>& packedLight,
                       const int direction,
                       BorderUpdateBatch& outBatch) {
    if (!job.chunk) {
        return;
    }

    if (direction == 0 || direction == 1) {
        const int x = (direction == 0) ? (Chunk::SIZE_X - 1) : 0;
        for (int y = 0; y < Chunk::SIZE_Y; ++y) {
            for (int z = 0; z < Chunk::SIZE_Z; ++z) {
                if (getOpacity(job, x, y, z) >= 15) {
                    continue;
                }

                const uint8_t sky = getSky(packedLight, x, y, z);
                if (sky > 1) {
                    BorderLightNode node;
                    node.localX = static_cast<uint8_t>(direction == 0 ? 0 : (Chunk::SIZE_X - 1));
                    node.y = static_cast<uint8_t>(y);
                    node.localZ = static_cast<uint8_t>(z);
                    node.level = static_cast<uint8_t>(sky - 1);
                    node.kind = LightKind::Sky;
                    outBatch.nodes.push_back(node);
                }

                const uint8_t block = getBlock(packedLight, x, y, z);
                if (block > 1) {
                    BorderLightNode node;
                    node.localX = static_cast<uint8_t>(direction == 0 ? 0 : (Chunk::SIZE_X - 1));
                    node.y = static_cast<uint8_t>(y);
                    node.localZ = static_cast<uint8_t>(z);
                    node.level = static_cast<uint8_t>(block - 1);
                    node.kind = LightKind::Block;
                    outBatch.nodes.push_back(node);
                }
            }
        }
        return;
    }

    const int z = (direction == 2) ? (Chunk::SIZE_Z - 1) : 0;
    for (int y = 0; y < Chunk::SIZE_Y; ++y) {
        for (int x = 0; x < Chunk::SIZE_X; ++x) {
            if (getOpacity(job, x, y, z) >= 15) {
                continue;
            }

            const uint8_t sky = getSky(packedLight, x, y, z);
            if (sky > 1) {
                BorderLightNode node;
                node.localX = static_cast<uint8_t>(x);
                node.y = static_cast<uint8_t>(y);
                node.localZ = static_cast<uint8_t>(direction == 2 ? 0 : (Chunk::SIZE_Z - 1));
                node.level = static_cast<uint8_t>(sky - 1);
                node.kind = LightKind::Sky;
                outBatch.nodes.push_back(node);
            }

            const uint8_t block = getBlock(packedLight, x, y, z);
            if (block > 1) {
                BorderLightNode node;
                node.localX = static_cast<uint8_t>(x);
                node.y = static_cast<uint8_t>(y);
                node.localZ = static_cast<uint8_t>(direction == 2 ? 0 : (Chunk::SIZE_Z - 1));
                node.level = static_cast<uint8_t>(block - 1);
                node.kind = LightKind::Block;
                outBatch.nodes.push_back(node);
            }
        }
    }
}

}

LightResult LightSolver::solve(const LightJob& job) {
    const auto startTime = std::chrono::steady_clock::now();

    LightResult result;
    result.selfDelta.chunkKey = job.chunkKey;
    result.selfDelta.revision = job.revision;

    if (!job.chunk) {
        return result;
    }

    std::vector<uint8_t> originalPacked = job.packedLightSnapshot;
    if (originalPacked.size() != Chunk::BLOCK_COUNT) {
        originalPacked.resize(Chunk::BLOCK_COUNT);
        for (int y = 0; y < Chunk::SIZE_Y; ++y) {
            for (int z = 0; z < Chunk::SIZE_Z; ++z) {
                for (int x = 0; x < Chunk::SIZE_X; ++x) {
                    originalPacked[packedIndex(x, y, z)] = job.chunk->getPackedLight(x, y, z);
                }
            }
        }
    }

    result.selfDelta.packedLight.assign(Chunk::BLOCK_COUNT, 0);

    std::deque<LightNode> skySpreadQueue;
    std::deque<LightNode> blockSpreadQueue;

    // Rebuild this chunk's local baseline sky by scanning each column top-down.
    // This keeps sky attenuation consistent for semi-transparent blocks (e.g. water).
    for (int z = 0; z < Chunk::SIZE_Z; ++z) {
        for (int x = 0; x < Chunk::SIZE_X; ++x) {
            uint8_t skyLevel = 15;
            for (int y = Chunk::SIZE_Y - 1; y >= 0; --y) {
                const BlockDef& def = BlockRegistry::get(getBlockId(job, x, y, z));
                if (def.opacity >= 15) {
                    skyLevel = 0;
                    continue;
                }

                if (skyLevel > 0) {
                    skyLevel = (skyLevel > def.opacity)
                        ? static_cast<uint8_t>(skyLevel - def.opacity)
                        : 0;
                }

                if (skyLevel == 0) {
                    continue;
                }

                setSky(result.selfDelta.packedLight, x, y, z, skyLevel);
                if (skyLevel > 1) {
                    skySpreadQueue.push_back({x, y, z});
                }
            }
        }
    }

    // Seed block-light propagation from local emitters.
    for (int y = 0; y < Chunk::SIZE_Y; ++y) {
        for (int z = 0; z < Chunk::SIZE_Z; ++z) {
            for (int x = 0; x < Chunk::SIZE_X; ++x) {
                const BlockDef& def = BlockRegistry::get(getBlockId(job, x, y, z));
                if (!def.isLightSource || def.lightLevel == 0) {
                    continue;
                }

                const uint8_t current = getBlock(result.selfDelta.packedLight, x, y, z);
                if (def.lightLevel > current) {
                    setBlock(result.selfDelta.packedLight, x, y, z, def.lightLevel);
                }
                blockSpreadQueue.push_back({x, y, z});
            }
        }
    }

    for (const BorderUpdateBatch& batch : job.inbox) {
        for (const BorderLightNode& node : batch.nodes) {
            const int x = static_cast<int>(node.localX);
            const int y = static_cast<int>(node.y);
            const int z = static_cast<int>(node.localZ);
            if (!isInside(x, y, z) || node.level == 0 || getOpacity(job, x, y, z) >= 15) {
                continue;
            }

            if (node.kind == LightKind::Sky) {
                if (node.level > getSky(result.selfDelta.packedLight, x, y, z)) {
                    setSky(result.selfDelta.packedLight, x, y, z, node.level);
                    skySpreadQueue.push_back({x, y, z});
                }
            } else {
                if (node.level > getBlock(result.selfDelta.packedLight, x, y, z)) {
                    setBlock(result.selfDelta.packedLight, x, y, z, node.level);
                    blockSpreadQueue.push_back({x, y, z});
                }
            }
        }
    }

    while (!skySpreadQueue.empty()) {
        const LightNode cur = skySpreadQueue.front();
        skySpreadQueue.pop_front();
        ++result.nodesVisited;

        const uint8_t curSky = getSky(result.selfDelta.packedLight, cur.x, cur.y, cur.z);
        if (curSky == 0) {
            continue;
        }

        for (int d = 0; d < 6; ++d) {
            const int nx = cur.x + DX[d];
            const int ny = cur.y + DY[d];
            const int nz = cur.z + DZ[d];
            if (!isInside(nx, ny, nz)) {
                continue;
            }

            const uint8_t opacity = getOpacity(job, nx, ny, nz);
            if (opacity >= 15) {
                continue;
            }

            uint8_t propagated = 0;
            if (DY[d] == -1) {
                propagated = (curSky > opacity) ? static_cast<uint8_t>(curSky - opacity) : 0;
            } else {
                const uint8_t attenuation = std::max<uint8_t>(1, opacity);
                propagated = (curSky > attenuation) ? static_cast<uint8_t>(curSky - attenuation) : 0;
            }
            if (propagated <= getSky(result.selfDelta.packedLight, nx, ny, nz)) {
                continue;
            }

            setSky(result.selfDelta.packedLight, nx, ny, nz, propagated);
            skySpreadQueue.push_back({nx, ny, nz});
        }
    }

    while (!blockSpreadQueue.empty()) {
        const LightNode cur = blockSpreadQueue.front();
        blockSpreadQueue.pop_front();
        ++result.nodesVisited;

        const uint8_t curBlock = getBlock(result.selfDelta.packedLight, cur.x, cur.y, cur.z);
        if (curBlock == 0) {
            continue;
        }

        for (int d = 0; d < 6; ++d) {
            const int nx = cur.x + DX[d];
            const int ny = cur.y + DY[d];
            const int nz = cur.z + DZ[d];
            if (!isInside(nx, ny, nz)) {
                continue;
            }

            const uint8_t opacity = getOpacity(job, nx, ny, nz);
            if (opacity >= 15) {
                continue;
            }

            const uint8_t attenuation = std::max<uint8_t>(1, opacity);
            const uint8_t propagated = (curBlock > attenuation)
                ? static_cast<uint8_t>(curBlock - attenuation)
                : 0;
            if (propagated <= getBlock(result.selfDelta.packedLight, nx, ny, nz)) {
                continue;
            }

            setBlock(result.selfDelta.packedLight, nx, ny, nz, propagated);
            blockSpreadQueue.push_back({nx, ny, nz});
        }
    }

    uint32_t dirtyMask = 0;
    for (int scy = 0; scy < Chunk::NUM_SUB_CHUNKS; ++scy) {
        const int yBegin = scy * Chunk::SUB_CHUNK_SIZE;
        const int yEnd = yBegin + Chunk::SUB_CHUNK_SIZE;
        bool subChunkChanged = false;

        for (int y = yBegin; y < yEnd && !subChunkChanged; ++y) {
            for (int z = 0; z < Chunk::SIZE_Z && !subChunkChanged; ++z) {
                for (int x = 0; x < Chunk::SIZE_X; ++x) {
                    const std::size_t idx = packedIndex(x, y, z);
                    if (result.selfDelta.packedLight[idx] != originalPacked[idx]) {
                        subChunkChanged = true;
                        break;
                    }
                }
            }
        }

        if (subChunkChanged) {
            dirtyMask |= (1u << scy);
        }
    }
    result.selfDelta.dirtySubChunkMask = dirtyMask;

    const std::shared_ptr<const Chunk> neighbors[4] = {
        job.neighborPosX,
        job.neighborNegX,
        job.neighborPosZ,
        job.neighborNegZ
    };
    for (int direction = 0; direction < 4; ++direction) {
        if (!neighbors[direction]) {
            continue;
        }

        BorderUpdateBatch batch;
        batch.targetChunkKey = static_cast<int64_t>(neighbors[direction]->m_chunkX) << 32 |
            (static_cast<int64_t>(neighbors[direction]->m_chunkZ) & 0xFFFFFFFF);
        batch.sourceRevision = job.revision;
        batch.fromDirection = static_cast<uint8_t>(direction);

        emitBoundarySeeds(job, result.selfDelta.packedLight, direction, batch);
        batch.dirtySubChunkMask = borderFaceDirtyMask(originalPacked, result.selfDelta.packedLight, direction);
        if (batch.dirtySubChunkMask != 0) {
            result.outgoing.push_back(std::move(batch));
        }
    }

    result.workerMs = static_cast<float>(std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - startTime).count());

    return result;
}




