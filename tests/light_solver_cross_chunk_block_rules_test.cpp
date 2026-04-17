#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

#include "../src/world/Chunk.h"
#include "../src/world/LightSolver.h"
#include "../src/world/World.h"

namespace {
[[noreturn]] void fail(const char* message) {
    std::cerr << "[light_solver_cross_chunk_block_rules_test] FAIL: " << message << '\n';
    std::cerr.flush();
    std::_Exit(EXIT_FAILURE);
}

std::vector<BlockID> snapshotBlocks(const Chunk& chunk) {
    std::vector<BlockID> blocks(Chunk::BLOCK_COUNT);
    for (int y = 0; y < Chunk::SIZE_Y; ++y) {
        for (int z = 0; z < Chunk::SIZE_Z; ++z) {
            for (int x = 0; x < Chunk::SIZE_X; ++x) {
                blocks[Chunk::toIndex(x, y, z)] = chunk.getBlock(x, y, z);
            }
        }
    }
    return blocks;
}

std::vector<uint8_t> snapshotPackedLight(const Chunk& chunk) {
    std::vector<uint8_t> packed(Chunk::BLOCK_COUNT);
    for (int y = 0; y < Chunk::SIZE_Y; ++y) {
        for (int z = 0; z < Chunk::SIZE_Z; ++z) {
            for (int x = 0; x < Chunk::SIZE_X; ++x) {
                packed[Chunk::toIndex(x, y, z)] = chunk.getPackedLight(x, y, z);
            }
        }
    }
    return packed;
}

LightJob buildJob(const std::shared_ptr<Chunk>& chunk) {
    LightJob job;
    job.chunkKey = World::chunkKey(chunk->m_chunkX, chunk->m_chunkZ);
    job.revision = chunk->getLightRevision();
    job.chunk = chunk;
    job.blockSnapshot = snapshotBlocks(*chunk);
    job.packedLightSnapshot = snapshotPackedLight(*chunk);
    return job;
}

uint8_t blockAt(const std::vector<uint8_t>& packed, const int x, const int y, const int z) {
    return static_cast<uint8_t>(packed[Chunk::toIndex(x, y, z)] & 0x0F);
}

BorderUpdateBatch findOutgoingToPosXNeighbor(const LightResult& result, const int64_t expectedKey) {
    const auto it = std::find_if(result.outgoing.begin(), result.outgoing.end(),
                                 [expectedKey](const BorderUpdateBatch& batch) {
                                     return batch.targetChunkKey == expectedKey && batch.fromDirection == 0;
                                 });
    if (it == result.outgoing.end()) {
        fail("expected +X outgoing boundary batch");
    }
    return *it;
}

void buildSealedTunnel(const std::shared_ptr<Chunk>& left, const std::shared_ptr<Chunk>& right, const int y, const int z) {
    for (int iy = 0; iy <= 40; ++iy) {
        for (int iz = 0; iz < Chunk::SIZE_Z; ++iz) {
            for (int ix = 0; ix < Chunk::SIZE_X; ++ix) {
                left->setBlockFast(ix, iy, iz, BlockIds::STONE);
                right->setBlockFast(ix, iy, iz, BlockIds::STONE);
            }
        }
    }

    for (int x = 13; x <= 15; ++x) {
        left->setBlockFast(x, y, z, BlockIds::AIR);
    }
    for (int x = 0; x <= 2; ++x) {
        right->setBlockFast(x, y, z, BlockIds::AIR);
    }
}

void testCrossChunkBlockLightNeedsPersistentBoundaryInput() {
    auto left = std::make_shared<Chunk>(0, 0);
    auto right = std::make_shared<Chunk>(1, 0);

    constexpr int y = 20;
    constexpr int z = 8;
    buildSealedTunnel(left, right, y, z);

    left->setBlockFast(14, y, z, BlockIds::TORCH);

    LightJob leftLitJob = buildJob(left);
    leftLitJob.neighborPosX = right;
    const LightResult leftLit = LightSolver::solve(leftLitJob);
    const BorderUpdateBatch litBoundary = findOutgoingToPosXNeighbor(leftLit, World::chunkKey(1, 0));

    LightJob rightLitJob = buildJob(right);
    rightLitJob.inbox.push_back(litBoundary);
    const LightResult rightLit = LightSolver::solve(rightLitJob);
    if (blockAt(rightLit.selfDelta.packedLight, 0, y, z) == 0 ||
        blockAt(rightLit.selfDelta.packedLight, 1, y, z) == 0) {
        fail("neighbor chunk should receive block light from +X border");
    }
    right->replacePackedLight(rightLit.selfDelta.packedLight.data(), rightLit.selfDelta.packedLight.size(), nullptr);

    // Simulate a later neighbor recompute while source light is still present.
    LightJob rightRecomputeWithCache = buildJob(right);
    rightRecomputeWithCache.inbox.push_back(litBoundary);
    const LightResult rightStillLit = LightSolver::solve(rightRecomputeWithCache);
    if (blockAt(rightStillLit.selfDelta.packedLight, 1, y, z) == 0) {
        fail("neighbor chunk should stay lit when boundary contribution is reused");
    }

    left->setBlockFast(14, y, z, BlockIds::AIR);
    LightJob leftRemovedJob = buildJob(left);
    leftRemovedJob.neighborPosX = right;
    const LightResult leftRemoved = LightSolver::solve(leftRemovedJob);
    const BorderUpdateBatch removedBoundary = findOutgoingToPosXNeighbor(leftRemoved, World::chunkKey(1, 0));

    LightJob rightRemovedJob = buildJob(right);
    rightRemovedJob.inbox.push_back(removedBoundary);
    const LightResult rightCleared = LightSolver::solve(rightRemovedJob);
    if (blockAt(rightCleared.selfDelta.packedLight, 0, y, z) != 0 ||
        blockAt(rightCleared.selfDelta.packedLight, 1, y, z) != 0) {
        fail("neighbor block light should clear after source torch removal");
    }
}
} // namespace

int main() {
    BlockRegistry::init(nullptr);

    testCrossChunkBlockLightNeedsPersistentBoundaryInput();

    std::cout << "[light_solver_cross_chunk_block_rules_test] PASS\n";
    return EXIT_SUCCESS;
}

