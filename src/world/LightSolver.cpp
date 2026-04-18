#include "LightSolver.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <utility>

#include "Block.h"
#include "Chunk.h"

namespace {
struct SolverContext {
    const LightJob& job;
    bool hasBlockSnapshot = false;
};

template <typename T>
class WorkQueue {
public:
    void reserve(const std::size_t capacity) {
        m_items.reserve(capacity);
    }

    bool empty() const {
        return m_head >= m_items.size();
    }

    void push(const T& value) {
        m_items.push_back(value);
    }

    void push(T&& value) {
        m_items.push_back(std::move(value));
    }

    T pop() {
        T value = m_items[m_head];
        ++m_head;
        if (m_head == m_items.size()) {
            m_items.clear();
            m_head = 0;
        }
        return value;
    }

private:
    std::vector<T> m_items;
    std::size_t m_head = 0;
};

struct LightNode {
    int x = 0;
    int y = 0;
    int z = 0;
};

struct RemovalNode {
    int x = 0;
    int y = 0;
    int z = 0;
    uint8_t level = 0;
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

uint8_t getLight(const std::vector<uint8_t>& packed,
                 const LightKind kind,
                 const int x,
                 const int y,
                 const int z) {
    return kind == LightKind::Sky ? getSky(packed, x, y, z) : getBlock(packed, x, y, z);
}

void setLight(std::vector<uint8_t>& packed,
              const LightKind kind,
              const int x,
              const int y,
              const int z,
              const uint8_t value) {
    if (kind == LightKind::Sky) {
        setSky(packed, x, y, z, value);
    } else {
        setBlock(packed, x, y, z, value);
    }
}

bool isInside(const int x, const int y, const int z) {
    return x >= 0 && x < Chunk::SIZE_X && y >= 0 && y < Chunk::SIZE_Y && z >= 0 && z < Chunk::SIZE_Z;
}

SolverContext makeSolverContext(const LightJob& job) {
    BlockRegistry::ensureInitialized();

    SolverContext context{
        job,
        job.blockSnapshot.size() == Chunk::BLOCK_COUNT
    };
    return context;
}

BlockID getBlockId(const SolverContext& context, const int x, const int y, const int z) {
    if (!isInside(x, y, z)) {
        return BlockIds::AIR;
    }
    if (context.hasBlockSnapshot) {
        return context.job.blockSnapshot[packedIndex(x, y, z)];
    }
    return BlockIds::AIR;
}

uint8_t getOpacity(SolverContext& context, const int x, const int y, const int z) {
    return BlockRegistry::getOpacityFast(getBlockId(context, x, y, z));
}

uint8_t propagateLevel(SolverContext& context,
                       const LightKind kind,
                       const uint8_t level,
                       const int direction,
                       const int nx,
                       const int ny,
                       const int nz) {
    if (!isInside(nx, ny, nz) || level == 0) {
        return 0;
    }

    const uint8_t opacity = getOpacity(context, nx, ny, nz);
    if (opacity >= 15) {
        return 0;
    }

    uint8_t attenuation = std::max<uint8_t>(1, opacity);
    if (kind == LightKind::Sky && DY[direction] == -1) {
        attenuation = opacity;
    }

    return level > attenuation ? static_cast<uint8_t>(level - attenuation) : 0;
}

std::vector<uint8_t> buildOriginalPacked(const LightJob& job) {
    if (job.packedLightSnapshot.size() == Chunk::BLOCK_COUNT) {
        return job.packedLightSnapshot;
    }
    return std::vector<uint8_t>(Chunk::BLOCK_COUNT, 0);
}

void applyBoundarySeeds(SolverContext& context,
                        const std::vector<BorderUpdateBatch>& batches,
                        std::vector<uint8_t>& packed) {
    for (const BorderUpdateBatch& batch : batches) {
        for (const BorderLightNode& node : batch.nodes) {
            const int x = static_cast<int>(node.localX);
            const int y = static_cast<int>(node.y);
            const int z = static_cast<int>(node.localZ);
            if (!isInside(x, y, z) || node.level == 0 || getOpacity(context, x, y, z) >= 15) {
                continue;
            }

            const uint8_t current = getLight(packed, node.kind, x, y, z);
            if (node.level > current) {
                setLight(packed, node.kind, x, y, z, node.level);
            }
        }
    }
}

void buildCurrentBasePacked(SolverContext& context, std::vector<uint8_t>& packed) {
    packed.assign(Chunk::BLOCK_COUNT, 0);

    for (int z = 0; z < Chunk::SIZE_Z; ++z) {
        for (int x = 0; x < Chunk::SIZE_X; ++x) {
            uint8_t skyLevel = 15;
            for (int y = Chunk::SIZE_Y - 1; y >= 0; --y) {
                const BlockID blockId = getBlockId(context, x, y, z);
                const uint8_t opacity = BlockRegistry::getOpacityFast(blockId);
                if (opacity >= 15) {
                    skyLevel = 0;
                    continue;
                }

                if (skyLevel > 0) {
                    skyLevel = (skyLevel > opacity)
                        ? static_cast<uint8_t>(skyLevel - opacity)
                        : 0;
                }

                if (skyLevel > 0) {
                    setSky(packed, x, y, z, skyLevel);
                }
            }
        }
    }

    for (int y = 0; y < Chunk::SIZE_Y; ++y) {
        for (int z = 0; z < Chunk::SIZE_Z; ++z) {
            for (int x = 0; x < Chunk::SIZE_X; ++x) {
                const BlockID blockId = getBlockId(context, x, y, z);
                if (!BlockRegistry::isLightSourceFast(blockId)) {
                    continue;
                }

                const uint8_t lightLevel = BlockRegistry::getLightLevelFast(blockId);
                if (lightLevel > 0 && lightLevel > getBlock(packed, x, y, z)) {
                    setBlock(packed, x, y, z, lightLevel);
                }
            }
        }
    }

    applyBoundarySeeds(context, context.job.inbox, packed);
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

void emitBoundarySeeds(SolverContext& context,
                       const std::vector<uint8_t>& packedLight,
                       const int direction,
                       BorderUpdateBatch& outBatch) {
    if (!context.job.chunk) {
        return;
    }

    if (direction == 0 || direction == 1) {
        const int x = (direction == 0) ? (Chunk::SIZE_X - 1) : 0;
        for (int y = 0; y < Chunk::SIZE_Y; ++y) {
            for (int z = 0; z < Chunk::SIZE_Z; ++z) {
                if (getOpacity(context, x, y, z) >= 15) {
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
            if (getOpacity(context, x, y, z) >= 15) {
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

void seedCellDiff(const std::vector<uint8_t>& basePacked,
                  std::vector<uint8_t>& workingPacked,
                  const LightKind kind,
                  const int x,
                  const int y,
                  const int z,
                  WorkQueue<RemovalNode>& removeQueue,
                  WorkQueue<LightNode>& addQueue) {
    if (!isInside(x, y, z)) {
        return;
    }

    const uint8_t current = getLight(workingPacked, kind, x, y, z);
    const uint8_t source = getLight(basePacked, kind, x, y, z);

    if (current > source) {
        setLight(workingPacked, kind, x, y, z, source);
        removeQueue.push({x, y, z, current});
        if (source > 0) {
            addQueue.push({x, y, z});
        }
    } else if (source > current) {
        setLight(workingPacked, kind, x, y, z, source);
        addQueue.push({x, y, z});
    }
}

void seedSkyColumnDiff(const std::vector<uint8_t>& basePacked,
                       std::vector<uint8_t>& workingPacked,
                       const int x,
                       const int z,
                       WorkQueue<RemovalNode>& removeQueue,
                       WorkQueue<LightNode>& addQueue) {
    for (int y = 0; y < Chunk::SIZE_Y; ++y) {
        seedCellDiff(basePacked, workingPacked, LightKind::Sky, x, y, z, removeQueue, addQueue);
    }
}

void seedNeighborReadds(const std::vector<uint8_t>& workingPacked,
                        const int x,
                        const int y,
                        const int z,
                        WorkQueue<LightNode>& skyAddQueue,
                        WorkQueue<LightNode>& blockAddQueue) {
    for (int d = 0; d < 6; ++d) {
        const int nx = x + DX[d];
        const int ny = y + DY[d];
        const int nz = z + DZ[d];
        if (!isInside(nx, ny, nz)) {
            continue;
        }

        if (getSky(workingPacked, nx, ny, nz) > 0) {
            skyAddQueue.push({nx, ny, nz});
        }
        if (getBlock(workingPacked, nx, ny, nz) > 0) {
            blockAddQueue.push({nx, ny, nz});
        }
    }
}

void seedBoundaryDiffs(const std::vector<uint8_t>& basePacked,
                       std::vector<uint8_t>& workingPacked,
                       const std::vector<uint8_t>& previousBoundaryPacked,
                       const std::vector<uint8_t>& currentBoundaryPacked,
                       const std::array<bool, 4>& changedDirections,
                       WorkQueue<RemovalNode>& skyRemoveQueue,
                       WorkQueue<RemovalNode>& blockRemoveQueue,
                       WorkQueue<LightNode>& skyAddQueue,
                       WorkQueue<LightNode>& blockAddQueue) {
    for (int direction = 0; direction < 4; ++direction) {
        if (!changedDirections[direction]) {
            continue;
        }

        if (direction == 0 || direction == 1) {
            const int x = (direction == 0) ? 0 : (Chunk::SIZE_X - 1);
            for (int y = 0; y < Chunk::SIZE_Y; ++y) {
                for (int z = 0; z < Chunk::SIZE_Z; ++z) {
                    if (getSky(previousBoundaryPacked, x, y, z) != getSky(currentBoundaryPacked, x, y, z)) {
                        seedCellDiff(basePacked, workingPacked, LightKind::Sky, x, y, z, skyRemoveQueue, skyAddQueue);
                    }
                    if (getBlock(previousBoundaryPacked, x, y, z) != getBlock(currentBoundaryPacked, x, y, z)) {
                        seedCellDiff(basePacked, workingPacked, LightKind::Block, x, y, z, blockRemoveQueue, blockAddQueue);
                    }
                }
            }
            continue;
        }

        const int z = (direction == 2) ? 0 : (Chunk::SIZE_Z - 1);
        for (int y = 0; y < Chunk::SIZE_Y; ++y) {
            for (int x = 0; x < Chunk::SIZE_X; ++x) {
                if (getSky(previousBoundaryPacked, x, y, z) != getSky(currentBoundaryPacked, x, y, z)) {
                    seedCellDiff(basePacked, workingPacked, LightKind::Sky, x, y, z, skyRemoveQueue, skyAddQueue);
                }
                if (getBlock(previousBoundaryPacked, x, y, z) != getBlock(currentBoundaryPacked, x, y, z)) {
                    seedCellDiff(basePacked, workingPacked, LightKind::Block, x, y, z, blockRemoveQueue, blockAddQueue);
                }
            }
        }
    }
}

void runRemovePass(SolverContext& context,
                   const std::vector<uint8_t>& basePacked,
                   std::vector<uint8_t>& workingPacked,
                   const LightKind kind,
                   WorkQueue<RemovalNode>& removeQueue,
                   WorkQueue<LightNode>& addQueue,
                   uint32_t& nodesVisited) {
    while (!removeQueue.empty()) {
        const RemovalNode cur = removeQueue.pop();
        ++nodesVisited;

        for (int d = 0; d < 6; ++d) {
            const int nx = cur.x + DX[d];
            const int ny = cur.y + DY[d];
            const int nz = cur.z + DZ[d];
            if (!isInside(nx, ny, nz)) {
                continue;
            }

            const uint8_t neighborLevel = getLight(workingPacked, kind, nx, ny, nz);
            if (neighborLevel == 0) {
                continue;
            }

            const uint8_t propagated = propagateLevel(context, kind, cur.level, d, nx, ny, nz);
            if (propagated == 0) {
                continue;
            }

            const uint8_t sourceLevel = getLight(basePacked, kind, nx, ny, nz);
            if (sourceLevel < neighborLevel && neighborLevel <= propagated) {
                setLight(workingPacked, kind, nx, ny, nz, sourceLevel);
                removeQueue.push({nx, ny, nz, neighborLevel});
                if (sourceLevel > 0) {
                    addQueue.push({nx, ny, nz});
                }
            } else {
                addQueue.push({nx, ny, nz});
            }
        }
    }
}

void runAddPass(SolverContext& context,
                std::vector<uint8_t>& packedLight,
                const LightKind kind,
                WorkQueue<LightNode>& addQueue,
                uint32_t& nodesVisited) {
    while (!addQueue.empty()) {
        const LightNode cur = addQueue.pop();
        ++nodesVisited;

        const uint8_t curLight = getLight(packedLight, kind, cur.x, cur.y, cur.z);
        if (curLight == 0) {
            continue;
        }

        for (int d = 0; d < 6; ++d) {
            const int nx = cur.x + DX[d];
            const int ny = cur.y + DY[d];
            const int nz = cur.z + DZ[d];
            if (!isInside(nx, ny, nz)) {
                continue;
            }

            const uint8_t propagated = propagateLevel(context, kind, curLight, d, nx, ny, nz);
            if (propagated <= getLight(packedLight, kind, nx, ny, nz)) {
                continue;
            }

            setLight(packedLight, kind, nx, ny, nz, propagated);
            addQueue.push({nx, ny, nz});
        }
    }
}

uint32_t computeDirtyMask(const std::vector<uint8_t>& before, const std::vector<uint8_t>& after) {
    uint32_t dirtyMask = 0;
    for (int scy = 0; scy < Chunk::NUM_SUB_CHUNKS; ++scy) {
        const int yBegin = scy * Chunk::SUB_CHUNK_SIZE;
        const int yEnd = yBegin + Chunk::SUB_CHUNK_SIZE;
        bool subChunkChanged = false;

        for (int y = yBegin; y < yEnd && !subChunkChanged; ++y) {
            for (int z = 0; z < Chunk::SIZE_Z && !subChunkChanged; ++z) {
                for (int x = 0; x < Chunk::SIZE_X; ++x) {
                    const std::size_t idx = packedIndex(x, y, z);
                    if (before[idx] != after[idx]) {
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
    return dirtyMask;
}

void buildOutgoingBatches(SolverContext& context,
                          const std::vector<uint8_t>& before,
                          const std::vector<uint8_t>& after,
                          std::vector<BorderUpdateBatch>& outgoing) {
    const std::shared_ptr<const Chunk> neighbors[4] = {
        context.job.neighborPosX,
        context.job.neighborNegX,
        context.job.neighborPosZ,
        context.job.neighborNegZ
    };

    for (int direction = 0; direction < 4; ++direction) {
        if (!neighbors[direction]) {
            continue;
        }

        BorderUpdateBatch batch;
        batch.targetChunkKey = static_cast<int64_t>(neighbors[direction]->m_chunkX) << 32 |
            (static_cast<int64_t>(neighbors[direction]->m_chunkZ) & 0xFFFFFFFF);
        batch.sourceRevision = context.job.revision;
        batch.fromDirection = static_cast<uint8_t>(direction);

        emitBoundarySeeds(context, after, direction, batch);
        batch.dirtySubChunkMask = borderFaceDirtyMask(before, after, direction);
        if (batch.dirtySubChunkMask != 0) {
            outgoing.push_back(std::move(batch));
        }
    }
}

bool hasChangedBoundary(const std::array<bool, 4>& changedDirections) {
    return std::any_of(changedDirections.begin(), changedDirections.end(),
                       [](const bool changed) { return changed; });
}

LightResult solveByRebuild(SolverContext& context,
                           const std::vector<uint8_t>& originalPacked,
                           const std::chrono::steady_clock::time_point startTime) {
    LightResult result;
    result.selfDelta.chunkKey = context.job.chunkKey;
    result.selfDelta.revision = context.job.revision;
    result.outgoing.reserve(4);

    buildCurrentBasePacked(context, result.selfDelta.packedLight);

    WorkQueue<LightNode> skyAddQueue;
    WorkQueue<LightNode> blockAddQueue;
    skyAddQueue.reserve(Chunk::BLOCK_COUNT);
    blockAddQueue.reserve(Chunk::BLOCK_COUNT);
    for (int y = 0; y < Chunk::SIZE_Y; ++y) {
        for (int z = 0; z < Chunk::SIZE_Z; ++z) {
            for (int x = 0; x < Chunk::SIZE_X; ++x) {
                if (getSky(result.selfDelta.packedLight, x, y, z) > 0) {
                    skyAddQueue.push({x, y, z});
                }
                if (getBlock(result.selfDelta.packedLight, x, y, z) > 0) {
                    blockAddQueue.push({x, y, z});
                }
            }
        }
    }

    runAddPass(context, result.selfDelta.packedLight, LightKind::Sky, skyAddQueue, result.nodesVisited);
    runAddPass(context, result.selfDelta.packedLight, LightKind::Block, blockAddQueue, result.nodesVisited);

    result.selfDelta.dirtySubChunkMask = computeDirtyMask(originalPacked, result.selfDelta.packedLight);
    buildOutgoingBatches(context, originalPacked, result.selfDelta.packedLight, result.outgoing);
    result.workerMs = static_cast<float>(std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - startTime).count());
    return result;
}
} // namespace

LightResult LightSolver::solve(const LightJob& job) {
    const auto startTime = std::chrono::steady_clock::now();

    if (!job.chunk) {
        LightResult empty;
        empty.selfDelta.chunkKey = job.chunkKey;
        empty.selfDelta.revision = job.revision;
        return empty;
    }

    SolverContext context = makeSolverContext(job);

    const std::vector<uint8_t> originalPacked = buildOriginalPacked(job);
    if (job.reason == LightDirtyReason::ChunkLoaded ||
        (!hasChangedBoundary(job.changedBoundaryDirections) && job.blockChanges.empty())) {
        return solveByRebuild(context, originalPacked, startTime);
    }

    LightResult result;
    result.selfDelta.chunkKey = job.chunkKey;
    result.selfDelta.revision = job.revision;
    result.outgoing.reserve(4);

    std::vector<uint8_t> basePacked;
    buildCurrentBasePacked(context, basePacked);

    std::vector<uint8_t> previousBoundaryPacked(Chunk::BLOCK_COUNT, 0);
    std::vector<uint8_t> currentBoundaryPacked(Chunk::BLOCK_COUNT, 0);
    applyBoundarySeeds(context, job.previousInbox, previousBoundaryPacked);
    applyBoundarySeeds(context, job.inbox, currentBoundaryPacked);

    result.selfDelta.packedLight = originalPacked;

    WorkQueue<RemovalNode> skyRemoveQueue;
    WorkQueue<RemovalNode> blockRemoveQueue;
    WorkQueue<LightNode> skyAddQueue;
    WorkQueue<LightNode> blockAddQueue;
    skyRemoveQueue.reserve(Chunk::BLOCK_COUNT);
    blockRemoveQueue.reserve(Chunk::BLOCK_COUNT);
    skyAddQueue.reserve(Chunk::BLOCK_COUNT);
    blockAddQueue.reserve(Chunk::BLOCK_COUNT);

    std::array<bool, Chunk::SIZE_X * Chunk::SIZE_Z> dirtySkyColumns{};
    for (const LocalLightChange& change : job.blockChanges) {
        const int x = static_cast<int>(change.localX);
        const int y = static_cast<int>(change.y);
        const int z = static_cast<int>(change.localZ);
        if (!isInside(x, y, z)) {
            continue;
        }

        dirtySkyColumns[static_cast<std::size_t>(x + z * Chunk::SIZE_X)] = true;
        seedCellDiff(basePacked, result.selfDelta.packedLight, LightKind::Block,
                     x, y, z, blockRemoveQueue, blockAddQueue);
        seedNeighborReadds(result.selfDelta.packedLight, x, y, z, skyAddQueue, blockAddQueue);
    }

    for (int z = 0; z < Chunk::SIZE_Z; ++z) {
        for (int x = 0; x < Chunk::SIZE_X; ++x) {
            if (!dirtySkyColumns[static_cast<std::size_t>(x + z * Chunk::SIZE_X)]) {
                continue;
            }
            seedSkyColumnDiff(basePacked, result.selfDelta.packedLight, x, z, skyRemoveQueue, skyAddQueue);
        }
    }

    seedBoundaryDiffs(basePacked,
                      result.selfDelta.packedLight,
                      previousBoundaryPacked,
                      currentBoundaryPacked,
                      job.changedBoundaryDirections,
                      skyRemoveQueue,
                      blockRemoveQueue,
                      skyAddQueue,
                      blockAddQueue);

    runRemovePass(context, basePacked, result.selfDelta.packedLight, LightKind::Sky,
                  skyRemoveQueue, skyAddQueue, result.nodesVisited);
    runRemovePass(context, basePacked, result.selfDelta.packedLight, LightKind::Block,
                  blockRemoveQueue, blockAddQueue, result.nodesVisited);
    runAddPass(context, result.selfDelta.packedLight, LightKind::Sky, skyAddQueue, result.nodesVisited);
    runAddPass(context, result.selfDelta.packedLight, LightKind::Block, blockAddQueue, result.nodesVisited);

    result.selfDelta.dirtySubChunkMask = computeDirtyMask(originalPacked, result.selfDelta.packedLight);
    buildOutgoingBatches(context, originalPacked, result.selfDelta.packedLight, result.outgoing);
    result.workerMs = static_cast<float>(std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - startTime).count());
    return result;
}
