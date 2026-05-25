#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

#include "../src/world/Chunk.h"
#include "../src/world/light/LightSolver.h"

namespace {
[[noreturn]] void fail(const char* message) {
    std::cerr << "[light_solver_sky_rules_test] FAIL: " << message << '\n';
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
    job.chunkKey = 0;
    job.revision = chunk->getLightRevision();
    job.chunk = chunk;
    job.blockSnapshot = snapshotBlocks(*chunk);
    job.packedLightSnapshot = snapshotPackedLight(*chunk);
    return job;
}

uint8_t skyAt(const std::vector<uint8_t>& packed, const int x, const int y, const int z) {
    return static_cast<uint8_t>((packed[Chunk::toIndex(x, y, z)] >> 4) & 0x0F);
}

void testWaterAttenuatesSky() {
    auto chunk = std::make_shared<Chunk>(0, 0);

    for (int y = 70; y <= 72; ++y) {
        chunk->setBlockFast(3, y, 3, BlockIds::WATER);
    }
    // Keep lower water voxels from being refreshed by side skylight so we can
    // assert pure downward attenuation.
    for (int y = 0; y <= 71; ++y) {
        chunk->setBlockFast(2, y, 3, BlockIds::STONE);
        chunk->setBlockFast(4, y, 3, BlockIds::STONE);
        chunk->setBlockFast(3, y, 2, BlockIds::STONE);
        chunk->setBlockFast(3, y, 4, BlockIds::STONE);
    }

    const LightResult result = LightSolver::solve(buildJob(chunk));
    const auto& packed = result.selfDelta.packedLight;

    if (skyAt(packed, 3, 72, 3) != 13) {
        fail("top water voxel should attenuate skylight from 15 to 13");
    }
    if (skyAt(packed, 3, 71, 3) != 11) {
        fail("second water voxel should continue attenuation to 11");
    }
    if (skyAt(packed, 3, 70, 3) != 9) {
        fail("third water voxel should continue attenuation to 9");
    }
}

void testSealedRoomBlocksSky() {
    auto chunk = std::make_shared<Chunk>(0, 0);

    for (int y = 59; y <= 61; ++y) {
        for (int z = 7; z <= 9; ++z) {
            for (int x = 7; x <= 9; ++x) {
                const bool shell = (x == 7 || x == 9 || y == 59 || y == 61 || z == 7 || z == 9);
                if (shell) {
                    chunk->setBlockFast(x, y, z, BlockIds::STONE);
                }
            }
        }
    }

    const LightResult result = LightSolver::solve(buildJob(chunk));
    const auto& packed = result.selfDelta.packedLight;

    if (skyAt(packed, 8, 60, 8) != 0) {
        fail("sealed room center should not receive skylight");
    }
}
} // namespace

int main() {
    BlockRegistry::init(nullptr);

    testWaterAttenuatesSky();
    testSealedRoomBlocksSky();

    std::cout << "[light_solver_sky_rules_test] PASS\n";
    return EXIT_SUCCESS;
}



