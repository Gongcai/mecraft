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

bool waitUntil(World& world,
               const glm::vec3& playerPos,
               const int maxFrames,
               const std::function<bool()>& predicate) {
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

    constexpr int perturbX = 20;
    constexpr int perturbY = 90;
    constexpr int perturbZ = 12;
    world.setBlock(perturbX, perturbY, perturbZ, BlockRegistry::requireIdByName("minecraft:stone"));
    tickWorld(world, playerPos, 2);

    if (!waitUntil(world, playerPos, 120, rightTunnelLit)) {
        fail("neighbor chunk should keep cross-chunk block light after its own recompute");
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

    pass();
}
