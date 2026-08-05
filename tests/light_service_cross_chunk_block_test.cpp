#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <thread>

#include <glm/vec3.hpp>

#include "../src/thread/ThreadPool.h"
#include "../src/world/World.h"

namespace {
[[noreturn]] void fail(const char* message) {
    std::cerr << "[light_service_cross_chunk_block_test] FAIL: " << message << '\n';
    std::cerr.flush();
    std::_Exit(EXIT_FAILURE);
}

[[noreturn]] void pass() {
    std::cout << "[light_service_cross_chunk_block_test] PASS\n";
    std::cout.flush();
    std::_Exit(EXIT_SUCCESS);
}

void tickWorld(World& world, const glm::vec3& playerPos, const int frames) {
    for (int i = 0; i < frames; ++i) {
        world.update(playerPos);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

bool waitUntil(World& world, const glm::vec3& playerPos, const int maxFrames, const std::function<bool()>& predicate) {
    for (int i = 0; i < maxFrames; ++i) {
        world.update(playerPos);
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return predicate();
}
} // namespace

int main() {
    BlockRegistry::init(nullptr);

    ThreadPool pool(2);
    pool.start();

    World world;
    world.init(20260417);
    world.setRenderDistance(1);
    world.setThreadPool(&pool);

    const glm::vec3 playerPos(8.0f, 80.0f, 8.0f);
    tickWorld(world, playerPos, 10);

    constexpr int tunnelY = 80;
    constexpr int tunnelZ = 8;
    for (int x = 14; x <= 17; ++x) {
        world.setBlock(x, tunnelY, tunnelZ, BlockRegistry::requireIdByName("minecraft:stone"));
    }
    tickWorld(world, playerPos, 20);

    constexpr int torchX = 14;
    constexpr int torchY = tunnelY + 1;
    world.setBlock(torchX, torchY, tunnelZ, BlockRegistry::requireIdByName("minecraft:torch"));
    tickWorld(world, playerPos, 2);

    const auto& chunks = world.getActiveChunks();
    const auto rightIt = chunks.find(World::chunkKey(1, 0));
    if (rightIt == chunks.end() || !rightIt->second) {
        fail("expected +X neighbor chunk to stay loaded");
    }

    Chunk& rightChunk = *rightIt->second;
    const auto rightTunnelLit = [&]() {
        const uint8_t borderLight = rightChunk.getBlockLight(0, torchY, tunnelZ);
        const uint8_t innerLight = rightChunk.getBlockLight(1, torchY, tunnelZ);
        return borderLight > 0 && innerLight > 0;
    };

    if (!waitUntil(world, playerPos, 120, rightTunnelLit)) {
        fail("torch should propagate block light into the neighboring chunk");
    }

    const auto leftIt = chunks.find(World::chunkKey(0, 0));
    if (leftIt == chunks.end() || !leftIt->second) {
        fail("expected source chunk to stay loaded");
    }
    const uint8_t torchLevel = BlockRegistry::getLightLevelFast(BlockRegistry::requireIdByName("minecraft:torch"));
    if (leftIt->second->getBlockLight(15, torchY, tunnelZ) != torchLevel - 1 ||
        rightChunk.getBlockLight(0, torchY, tunnelZ) != torchLevel - 2 ||
        rightChunk.getBlockLight(1, torchY, tunnelZ) != torchLevel - 3) {
        fail("cross-chunk block-light gradient should attenuate by one per voxel without a boundary discontinuity");
    }

    constexpr int perturbX = 20;
    constexpr int perturbY = 90;
    constexpr int perturbZ = 12;
    world.setBlock(perturbX, perturbY, perturbZ, BlockRegistry::requireIdByName("minecraft:stone"));
    tickWorld(world, playerPos, 2);

    if (!waitUntil(world, playerPos, 120, rightTunnelLit)) {
        fail("neighbor chunk should keep cross-chunk block light after its own recompute");
    }
    if (leftIt->second->getBlockLight(15, torchY, tunnelZ) != torchLevel - 1 ||
        rightChunk.getBlockLight(0, torchY, tunnelZ) != torchLevel - 2 ||
        rightChunk.getBlockLight(1, torchY, tunnelZ) != torchLevel - 3) {
        fail("neighbor recompute should preserve the exact cross-chunk block-light gradient");
    }

    world.setBlock(torchX, torchY, tunnelZ, RUNTIME_ID_NULL);
    tickWorld(world, playerPos, 2);

    const bool clearedInNeighbor = waitUntil(world, playerPos, 180, [&]() {
        const uint8_t borderLight = rightChunk.getBlockLight(0, torchY, tunnelZ);
        const uint8_t innerLight = rightChunk.getBlockLight(1, torchY, tunnelZ);
        return borderLight == 0 && innerLight == 0;
    });
    if (!clearedInNeighbor) {
        fail("neighboring chunk block light should clear after removing the source torch");
    }

    constexpr int rightTorchX = 17;
    world.setBlock(torchX, torchY, tunnelZ, BlockRegistry::requireIdByName("minecraft:torch"));
    world.setBlock(rightTorchX, torchY, tunnelZ, BlockRegistry::requireIdByName("minecraft:torch"));
    if (!waitUntil(world, playerPos, 180, [&]() {
            return leftIt->second->getBlockLight(15, torchY, tunnelZ) == torchLevel - 1 &&
                   rightChunk.getBlockLight(0, torchY, tunnelZ) == torchLevel - 1 &&
                   isLightFrameSettled(world.getLightFrameStats());
        })) {
        fail("opposing cross-chunk light sources should settle before removal");
    }

    world.setBlock(torchX, torchY, tunnelZ, RUNTIME_ID_NULL);
    if (!waitUntil(world, playerPos, 240, [&]() {
            return rightChunk.getBlockLight(1, torchY, tunnelZ) == torchLevel &&
                   rightChunk.getBlockLight(0, torchY, tunnelZ) == torchLevel - 1 &&
                   leftIt->second->getBlockLight(15, torchY, tunnelZ) == torchLevel - 2 &&
                   leftIt->second->getBlockLight(14, torchY, tunnelZ) == torchLevel - 3 &&
                   isLightFrameSettled(world.getLightFrameStats());
        })) {
        fail("independent neighbor light should cross the boundary continuously after local emitter removal");
    }

    pass();
}
