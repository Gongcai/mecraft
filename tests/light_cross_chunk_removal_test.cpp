#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <thread>

#include <glm/vec3.hpp>

#include "../src/thread/ThreadPool.h"
#include "../src/world/World.h"

namespace {
struct ThreadPoolGuard {
    ThreadPool& pool;
    ~ThreadPoolGuard() { pool.shutdown(); }
};

[[noreturn]] void fail(const char* message) {
    std::cerr << "[light_cross_chunk_removal_test] FAIL: " << message << '\n';
    std::cerr.flush();
    std::_Exit(EXIT_FAILURE);
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

void clearChunkDirtyFlags(World& world) {
    for (const auto& entry : world.getActiveChunks()) {
        if (entry.second) {
            entry.second->markMeshClean();
        }
    }
}
} // namespace

int main() {
    BlockRegistry::init(nullptr);

    ThreadPool pool(2);
    pool.start();
    ThreadPoolGuard poolGuard{pool};

    World world;
    world.init(20260417);
    world.setRenderDistance(1);
    world.setThreadPool(&pool);

    const glm::vec3 playerPos(8.0f, 80.0f, 8.0f);
    tickWorld(world, playerPos, 10);

    constexpr int y = 80;
    constexpr int z = 8;
    for (int x = 14; x <= 17; ++x) {
        world.setBlock(x, y, z, BlockRegistry::requireIdByName("minecraft:stone"));
    }
    tickWorld(world, playerPos, 20);

    clearChunkDirtyFlags(world);

    constexpr int torchX = 14;
    constexpr int torchY = y + 1;
    world.setBlock(torchX, torchY, z, BlockRegistry::requireIdByName("minecraft:torch"));
    tickWorld(world, playerPos, 2);

    const auto& chunksAfterPlace = world.getActiveChunks();
    const auto rightIt = chunksAfterPlace.find(World::chunkKey(1, 0));
    if (rightIt == chunksAfterPlace.end() || !rightIt->second) {
        fail("expected +X neighbor chunk to stay loaded");
    }

    Chunk& rightChunk = *rightIt->second;
    const bool propagatedToNeighbor = waitUntil(world, playerPos, 120, [&]() {
        const uint8_t borderLight = rightChunk.getBlockLight(0, torchY, z);
        const uint8_t innerLight = rightChunk.getBlockLight(1, torchY, z);
        return borderLight > 0 && innerLight > 0;
    });
    if (!propagatedToNeighbor) {
        fail("torch should propagate block light into the neighboring chunk before removal");
    }

    const int scy = Chunk::toSubChunkIndex(torchY);
    if (!rightChunk.isSubChunkDirty(scy)) {
        fail("neighboring chunk light apply should dirty the mesh that samples changed cross-chunk halo light");
    }

    clearChunkDirtyFlags(world);

    world.setBlock(torchX, torchY, z, RUNTIME_ID_NULL);
    tickWorld(world, playerPos, 2);

    const bool clearedInNeighbor = waitUntil(world, playerPos, 180, [&]() {
        const uint8_t borderLight = rightChunk.getBlockLight(0, torchY, z);
        const uint8_t innerLight = rightChunk.getBlockLight(1, torchY, z);
        return borderLight == 0 && innerLight == 0;
    });
    if (!clearedInNeighbor) {
        fail("neighboring chunk block light should be cleared after removing the torch");
    }

    if (!rightChunk.isSubChunkDirty(scy)) {
        fail("neighboring chunk should be marked dirty after cross-chunk light removal");
    }

    std::cout << "[light_cross_chunk_removal_test] PASS\n";
    return EXIT_SUCCESS;
}
