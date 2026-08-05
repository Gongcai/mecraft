#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

#include "../src/world/chunk/Chunk.h"
#include "../src/world/light/LightSolver.h"
#include "../src/world/World.h"

namespace {
[[noreturn]] void fail(const char* message) {
    std::cerr << "[light_solver_cross_chunk_block_rules_test] FAIL: " << message << '\n';
    std::cerr.flush();
    std::_Exit(EXIT_FAILURE);
}

BlockStateId stateForBlockId(const BlockID blockId) {
    return blockId == RUNTIME_ID_NULL ? NULL_BLOCK_STATE : BlockStateRegistry::getDefaultState(blockId);
}

BlockStateId stateForBlockName(const char* name) {
    return stateForBlockId(BlockRegistry::requireIdByName(name));
}

std::vector<BlockID> snapshotBlocks(const Chunk& chunk) {
    std::vector<BlockID> blocks(Chunk::BLOCK_COUNT);
    for (int y = 0; y < Chunk::SIZE_Y; ++y) {
        for (int z = 0; z < Chunk::SIZE_Z; ++z) {
            for (int x = 0; x < Chunk::SIZE_X; ++x) {
                blocks[Chunk::toIndex(x, y, z)] = BlockStateRegistry::getBlockId(chunk.getBlock(x, y, z));
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
    const auto it =
        std::find_if(result.outgoing.begin(), result.outgoing.end(), [expectedKey](const BorderUpdateBatch& batch) {
            return batch.targetChunkKey == expectedKey && batch.fromDirection == 0;
        });
    if (it == result.outgoing.end()) {
        fail("expected +X outgoing boundary batch");
    }
    return *it;
}

BorderUpdateBatch findOutgoingToNegXNeighbor(const LightResult& result, const int64_t expectedKey) {
    const auto it =
        std::find_if(result.outgoing.begin(), result.outgoing.end(), [expectedKey](const BorderUpdateBatch& batch) {
            return batch.targetChunkKey == expectedKey && batch.fromDirection == 1;
        });
    if (it == result.outgoing.end()) {
        fail("expected -X outgoing boundary batch");
    }
    return *it;
}

void buildSealedTunnel(const std::shared_ptr<Chunk>& left, const std::shared_ptr<Chunk>& right, const int y,
                       const int z) {
    for (int iy = 0; iy <= 40; ++iy) {
        for (int iz = 0; iz < Chunk::SIZE_Z; ++iz) {
            for (int ix = 0; ix < Chunk::SIZE_X; ++ix) {
                left->setBlockFast(ix, iy, iz, stateForBlockName("minecraft:stone"));
                right->setBlockFast(ix, iy, iz, stateForBlockName("minecraft:stone"));
            }
        }
    }

    for (int x = 13; x <= 15; ++x) {
        left->setBlockFast(x, y, z, NULL_BLOCK_STATE);
    }
    for (int x = 0; x <= 2; ++x) {
        right->setBlockFast(x, y, z, NULL_BLOCK_STATE);
    }
}

void testCrossChunkBlockLightNeedsPersistentBoundaryInput() {
    auto left = std::make_shared<Chunk>(0, 0);
    auto right = std::make_shared<Chunk>(1, 0);

    constexpr int y = 20;
    constexpr int z = 8;
    buildSealedTunnel(left, right, y, z);

    left->setBlockFast(14, y, z, stateForBlockName("minecraft:torch"));

    LightJob leftLitJob = buildJob(left);
    leftLitJob.neighborPosX = right;
    const LightResult leftLit = LightSolver::solve(leftLitJob);
    const BorderUpdateBatch litBoundary = findOutgoingToPosXNeighbor(leftLit, World::chunkKey(1, 0));
    left->replacePackedLight(leftLit.selfDelta.packedLight.data(), leftLit.selfDelta.packedLight.size(), nullptr);

    LightJob leftStableSyncJob = buildJob(left);
    leftStableSyncJob.neighborPosX = right;
    leftStableSyncJob.forceOutgoingBoundaryMask = 1u << 0;
    const LightResult leftStableSync = LightSolver::solve(leftStableSyncJob);
    const BorderUpdateBatch stableBoundary = findOutgoingToPosXNeighbor(leftStableSync, World::chunkKey(1, 0));
    if (leftStableSync.nodesVisited != 0) {
        fail("stable forced boundary sync should not rebuild the chunk light field");
    }
    if (stableBoundary.dirtySubChunkMask != 0 || stableBoundary.nodes.empty()) {
        fail("forced boundary sync should resend stable border light without reporting a light delta");
    }

    LightJob rightLitJob = buildJob(right);
    rightLitJob.reason = LightDirtyReason::NeighborBoundary;
    rightLitJob.changedBoundaryDirections[0] = true;
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

    left->setBlockFast(14, y, z, NULL_BLOCK_STATE);
    LightJob leftRemovedJob = buildJob(left);
    leftRemovedJob.neighborPosX = right;
    const LightResult leftRemoved = LightSolver::solve(leftRemovedJob);
    const BorderUpdateBatch removedBoundary = findOutgoingToPosXNeighbor(leftRemoved, World::chunkKey(1, 0));

    right->setBlockLight(2, y, z, 15);
    LightJob rightRemovedJob = buildJob(right);
    rightRemovedJob.reason = LightDirtyReason::NeighborBoundary;
    rightRemovedJob.changedBoundaryDirections[0] = true;
    rightRemovedJob.previousInbox.push_back(litBoundary);
    rightRemovedJob.inbox.push_back(removedBoundary);
    const LightResult rightCleared = LightSolver::solve(rightRemovedJob);
    if (blockAt(rightCleared.selfDelta.packedLight, 0, y, z) != 0 ||
        blockAt(rightCleared.selfDelta.packedLight, 1, y, z) != 0 ||
        blockAt(rightCleared.selfDelta.packedLight, 2, y, z) != 0) {
        fail("neighbor block light should clear after source torch removal");
    }
}

void testStaleReturnedBoundaryDoesNotPreserveRemovedLocalEmitter() {
    auto left = std::make_shared<Chunk>(0, 0);
    auto right = std::make_shared<Chunk>(1, 0);

    constexpr int y = 20;
    constexpr int z = 8;
    buildSealedTunnel(left, right, y, z);
    left->setBlockFast(14, y, z, stateForBlockName("minecraft:torch"));

    LightJob leftLitJob = buildJob(left);
    leftLitJob.neighborPosX = right;
    const LightResult leftLit = LightSolver::solve(leftLitJob);
    const BorderUpdateBatch litBoundary = findOutgoingToPosXNeighbor(leftLit, World::chunkKey(1, 0));
    left->replacePackedLight(leftLit.selfDelta.packedLight.data(), leftLit.selfDelta.packedLight.size(), nullptr);

    LightJob rightLitJob = buildJob(right);
    rightLitJob.reason = LightDirtyReason::NeighborBoundary;
    rightLitJob.changedBoundaryDirections[0] = true;
    rightLitJob.inbox.push_back(litBoundary);
    const LightResult rightLit = LightSolver::solve(rightLitJob);
    right->replacePackedLight(rightLit.selfDelta.packedLight.data(), rightLit.selfDelta.packedLight.size(), nullptr);

    LightJob staleRightSyncJob = buildJob(right);
    staleRightSyncJob.neighborNegX = left;
    staleRightSyncJob.inbox.push_back(litBoundary);
    staleRightSyncJob.forceOutgoingBoundaryMask = 1u << 1;
    const LightResult staleRightSync = LightSolver::solve(staleRightSyncJob);
    const BorderUpdateBatch staleReturnedBoundary =
        findOutgoingToNegXNeighbor(staleRightSync, World::chunkKey(0, 0));

    left->setBlockFast(14, y, z, NULL_BLOCK_STATE);
    LightJob leftRemovedRaceJob = buildJob(left);
    leftRemovedRaceJob.reason = LightDirtyReason::BlockChanged;
    leftRemovedRaceJob.blockChanges.push_back({static_cast<uint8_t>(14), static_cast<uint8_t>(y),
                                               static_cast<uint8_t>(z),
                                               BlockRegistry::requireIdByName("minecraft:torch"), RUNTIME_ID_NULL});
    leftRemovedRaceJob.changedBoundaryDirections[1] = true;
    leftRemovedRaceJob.suppressedBoundaryMask = 1u << 1;
    leftRemovedRaceJob.inbox.push_back(staleReturnedBoundary);

    const LightResult leftAfterRace = LightSolver::solve(leftRemovedRaceJob);
    if (blockAt(leftAfterRace.selfDelta.packedLight, 14, y, z) != 0 ||
        blockAt(leftAfterRace.selfDelta.packedLight, 15, y, z) != 0) {
        fail("stale returned boundary should not preserve removed local emitter light");
    }
}

void testEmissionRemovalRebuildsOverestimatedResidualLight() {
    auto chunk = std::make_shared<Chunk>(0, 0);

    constexpr int y = 20;
    constexpr int z = 8;
    for (int iy = 0; iy <= 40; ++iy) {
        for (int iz = 0; iz < Chunk::SIZE_Z; ++iz) {
            for (int ix = 0; ix < Chunk::SIZE_X; ++ix) {
                chunk->setBlockFast(ix, iy, iz, stateForBlockName("minecraft:stone"));
            }
        }
    }
    for (int x = 6; x <= 10; ++x) {
        chunk->setBlockFast(x, y, z, NULL_BLOCK_STATE);
    }
    chunk->setBlockFast(8, y, z, stateForBlockName("minecraft:torch"));

    const LightResult lit = LightSolver::solve(buildJob(chunk));
    chunk->replacePackedLight(lit.selfDelta.packedLight.data(), lit.selfDelta.packedLight.size(), nullptr);
    chunk->setBlockLight(10, y, z, 15);
    chunk->setBlockFast(8, y, z, NULL_BLOCK_STATE);

    LightJob removedJob = buildJob(chunk);
    removedJob.reason = LightDirtyReason::BlockChanged;
    removedJob.blockChanges.push_back({static_cast<uint8_t>(8), static_cast<uint8_t>(y), static_cast<uint8_t>(z),
                                       BlockRegistry::requireIdByName("minecraft:torch"), RUNTIME_ID_NULL});
    removedJob.suppressedBoundaryMask = 0x0Fu;

    const LightResult cleared = LightSolver::solve(removedJob);
    if (blockAt(cleared.selfDelta.packedLight, 8, y, z) != 0 ||
        blockAt(cleared.selfDelta.packedLight, 9, y, z) != 0 ||
        blockAt(cleared.selfDelta.packedLight, 10, y, z) != 0) {
        fail("emission removal should clear residual light above the old propagation gradient");
    }
}

void testGlowLichenPropagationAndRemovalUsesRemovePass() {
    auto chunk = std::make_shared<Chunk>(0, 0);

    constexpr int y = 20;
    constexpr int z = 8;
    for (int iy = 0; iy <= 40; ++iy) {
        for (int iz = 0; iz < Chunk::SIZE_Z; ++iz) {
            for (int ix = 0; ix < Chunk::SIZE_X; ++ix) {
                chunk->setBlockFast(ix, iy, iz, stateForBlockName("minecraft:stone"));
            }
        }
    }
    for (int x = 6; x <= 10; ++x) {
        chunk->setBlockFast(x, y, z, NULL_BLOCK_STATE);
    }
    chunk->setBlockFast(8, y, z, stateForBlockName("minecraft:glow_lichen"));

    const LightResult lit = LightSolver::solve(buildJob(chunk));
    if (blockAt(lit.selfDelta.packedLight, 9, y, z) == 0 || blockAt(lit.selfDelta.packedLight, 10, y, z) == 0) {
        fail("glow lichen block light should propagate through the local tunnel");
    }
    chunk->replacePackedLight(lit.selfDelta.packedLight.data(), lit.selfDelta.packedLight.size(), nullptr);

    chunk->setBlockFast(8, y, z, NULL_BLOCK_STATE);
    LightJob removedJob = buildJob(chunk);
    removedJob.reason = LightDirtyReason::BlockChanged;
    removedJob.blockChanges.push_back({static_cast<uint8_t>(8), static_cast<uint8_t>(y), static_cast<uint8_t>(z),
                                       BlockRegistry::requireIdByName("minecraft:glow_lichen"), RUNTIME_ID_NULL});

    const LightResult cleared = LightSolver::solve(removedJob);
    if (blockAt(cleared.selfDelta.packedLight, 8, y, z) != 0 || blockAt(cleared.selfDelta.packedLight, 9, y, z) != 0 ||
        blockAt(cleared.selfDelta.packedLight, 10, y, z) != 0) {
        fail("glow lichen block light should clear after deleting the emitter");
    }
}
} // namespace

int main() {
    BlockRegistry::init(nullptr);

    testCrossChunkBlockLightNeedsPersistentBoundaryInput();
    testStaleReturnedBoundaryDoesNotPreserveRemovedLocalEmitter();
    testEmissionRemovalRebuildsOverestimatedResidualLight();
    testGlowLichenPropagationAndRemovalUsesRemovePass();

    std::cout << "[light_solver_cross_chunk_block_rules_test] PASS\n";
    return EXIT_SUCCESS;
}
