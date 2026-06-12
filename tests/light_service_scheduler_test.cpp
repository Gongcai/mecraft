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
    ~ThreadPoolGuard() {
        pool.shutdown();
    }
};

[[noreturn]] void fail(const char* message) {
    std::cerr << "[light_service_scheduler_test] FAIL: " << message << '\n';
    std::cerr.flush();
    std::_Exit(EXIT_FAILURE);
}

[[noreturn]] void pass() {
    std::cout << "[light_service_scheduler_test] PASS\n";
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

bool hasChunk(const World& world, const int cx, const int cz) {
    return world.getActiveChunks().find(World::chunkKey(cx, cz)) != world.getActiveChunks().end();
}

Chunk* findChunk(World& world, const int cx, const int cz) {
    const auto it = world.getActiveChunks().find(World::chunkKey(cx, cz));
    return it != world.getActiveChunks().end() && it->second ? it->second.get() : nullptr;
}

void settleLoadedArea(World& world, const glm::vec3& playerPos) {
    tickWorld(world, playerPos, 30);
}

void testAdjacentChunksCanBeInFlightTogether() {
    ThreadPool pool(2);
    pool.start();

    World world;
    ThreadPoolGuard poolGuard{pool};
    world.init(20260417);
    world.setRenderDistance(1);
    world.setThreadPool(&pool);

    const glm::vec3 playerPos(8.0f, 80.0f, 8.0f);
    tickWorld(world, playerPos, 10);
    settleLoadedArea(world, playerPos);

    constexpr int y = 80;
    constexpr int zA = 8;
    constexpr int zB = 10;
    for (int x = 13; x <= 18; ++x) {
        world.setBlock(x, y, zA, BlockIds::STONE);
        world.setBlock(x, y, zB, BlockIds::STONE);
    }
    tickWorld(world, playerPos, 20);
    settleLoadedArea(world, playerPos);

    constexpr int torchY = y + 1;
    world.setBlock(14, torchY, zA, BlockIds::TORCH);
    world.setBlock(17, torchY, zB, BlockIds::TORCH);

    bool sawConcurrentSubmit = false;
    bool bothCrossChunkLit = false;
    for (int i = 0; i < 180; ++i) {
        world.update(playerPos);
        const LightFrameStats stats = world.getLightFrameStats();
        if (stats.submittedBlockChanged >= 2 || stats.inFlight >= 2) {
            sawConcurrentSubmit = true;
        }

        Chunk* leftChunk = findChunk(world, 0, 0);
        Chunk* rightChunk = findChunk(world, 1, 0);
        if (leftChunk && rightChunk) {
            const bool leftToRight = rightChunk->getBlockLight(0, torchY, zA) > 0 &&
                                     rightChunk->getBlockLight(1, torchY, zA) > 0;
            const bool rightToLeft = leftChunk->getBlockLight(15, torchY, zB) > 0 &&
                                     leftChunk->getBlockLight(14, torchY, zB) > 0;
            bothCrossChunkLit = leftToRight && rightToLeft;
        }

        if (sawConcurrentSubmit && bothCrossChunkLit) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (!sawConcurrentSubmit) {
        fail("adjacent dirty chunks should be submitted concurrently");
    }
    if (!bothCrossChunkLit) {
        fail("adjacent chunks should both finish correct cross-chunk propagation");
    }
}

void testUnloadDropsLateLightingResults() {
    ThreadPool pool(1);
    pool.start();

    World world;
    ThreadPoolGuard poolGuard{pool};
    world.init(20260417);
    world.setRenderDistance(1);
    world.setThreadPool(&pool);

    const glm::vec3 nearPos(8.0f, 80.0f, 8.0f);
    const glm::vec3 farPos(168.0f, 80.0f, 168.0f);
    tickWorld(world, nearPos, 10);
    settleLoadedArea(world, nearPos);

    constexpr int baseY = 80;
    constexpr int torchY = baseY + 1;
    const struct Seed {
        int x;
        int z;
    } seeds[] = {
        {8, 8},
        {24, 8},
        {8, 24},
        {24, 24},
    };

    for (const Seed& seed : seeds) {
        world.setBlock(seed.x, baseY, seed.z, BlockIds::STONE);
        world.setBlock(seed.x, torchY, seed.z, BlockIds::TORCH);
    }

    world.update(nearPos);
    if (world.getLightFrameStats().submitted == 0) {
        fail("expected dirty chunks to submit lighting work before unload");
    }

    bool sawStaleDrop = false;
    if (!waitUntil(world, farPos, 30, [&]() {
            if (world.getLightFrameStats().staleDropped > 0) {
                sawStaleDrop = true;
            }
            return !hasChunk(world, 0, 0);
        })) {
        fail("origin chunk should unload after moving far away");
    }

    for (int i = 0; i < 240; ++i) {
        world.update(farPos);
        if (world.getLightFrameStats().staleDropped > 0) {
            sawStaleDrop = true;
        }
        if (sawStaleDrop && pool.activeCount() == 0 && pool.pendingCount() == 0) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (!sawStaleDrop) {
        fail("late lighting results should be dropped after chunk unload");
    }

    if (!waitUntil(world, nearPos, 80, [&]() { return hasChunk(world, 0, 0); })) {
        fail("origin chunk should reload after returning");
    }

    const bool cleanReload = waitUntil(world, nearPos, 120, [&]() {
        Chunk* chunk = findChunk(world, 0, 0);
        return chunk != nullptr &&
               world.getBlock(8, torchY, 8) != BlockIds::TORCH &&
               chunk->getBlockLight(8, torchY, 8) == 0;
    });
    if (!cleanReload) {
        fail("reloaded chunk should not be polluted by stale lighting results");
    }
}

void testHighFrequencyContinuousBlockChangesRequeueCleanly() {
    ThreadPool pool(1);
    pool.start();

    World world;
    ThreadPoolGuard poolGuard{pool};
    world.init(20260417);
    world.setRenderDistance(1);
    world.setThreadPool(&pool);

    const glm::vec3 playerPos(8.0f, 80.0f, 8.0f);
    tickWorld(world, playerPos, 10);
    settleLoadedArea(world, playerPos);

    constexpr int baseY = 80;
    constexpr int torchY = baseY + 1;
    constexpr int torchX = 8;
    constexpr int torchZ = 8;
    constexpr int helperX = 9;

    world.setBlock(torchX, baseY, torchZ, BlockIds::STONE);
    world.setBlock(helperX, baseY, torchZ, BlockIds::STONE);
    world.setBlock(24, baseY, 8, BlockIds::STONE);
    world.setBlock(24, torchY, 8, BlockIds::TORCH);
    world.setBlock(8, baseY, 24, BlockIds::STONE);
    world.setBlock(8, torchY, 24, BlockIds::TORCH);
    world.setBlock(24, baseY, 24, BlockIds::STONE);
    world.setBlock(24, torchY, 24, BlockIds::TORCH);
    tickWorld(world, playerPos, 10);
    settleLoadedArea(world, playerPos);

    world.setBlock(torchX, torchY, torchZ, BlockIds::TORCH);
    world.update(playerPos);

    bool sawRequeue = false;
    for (int i = 0; i < 48; ++i) {
        world.setBlock(torchX, torchY, torchZ, (i % 2 == 0) ? BlockIds::TORCH : BlockIds::AIR);
        world.setBlock(helperX, torchY, torchZ, (i % 3 == 0) ? BlockIds::STONE : BlockIds::AIR);
    }

    world.setBlock(torchX, torchY, torchZ, BlockIds::AIR);
    world.setBlock(helperX, torchY, torchZ, BlockIds::AIR);

    for (int i = 0; i < 180; ++i) {
        world.update(playerPos);
        if (world.getLightFrameStats().requeued > 0) {
            sawRequeue = true;
        }
        Chunk* chunk = findChunk(world, 0, 0);
        const bool finalStateClean = chunk != nullptr &&
            world.getBlock(torchX, torchY, torchZ) == BlockIds::AIR &&
            chunk->getBlockLight(torchX, torchY, torchZ) == 0 &&
            chunk->getBlockLight(helperX, torchY, torchZ) == 0;
        if (sawRequeue && finalStateClean) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (!sawRequeue) {
        fail("same chunk should requeue lighting when blocks change continuously");
    }
    Chunk* chunk = findChunk(world, 0, 0);
    const bool finalStateClean = chunk != nullptr &&
        world.getBlock(torchX, torchY, torchZ) == BlockIds::AIR &&
        chunk->getBlockLight(torchX, torchY, torchZ) == 0 &&
        chunk->getBlockLight(helperX, torchY, torchZ) == 0;
    if (!finalStateClean) {
        fail("final lighting should match the last high-frequency block edits");
    }
}

void testInteriorBlockChangeOnlyQueuesOwningChunk() {
    ThreadPool pool(2);
    pool.start();

    World world;
    ThreadPoolGuard poolGuard{pool};
    world.init(20260417);
    world.setRenderDistance(1);
    world.setThreadPool(&pool);

    const glm::vec3 playerPos(8.0f, 80.0f, 8.0f);
    tickWorld(world, playerPos, 10);

    const bool settled = waitUntil(world, playerPos, 240, [&]() {
        const LightFrameStats stats = world.getLightFrameStats();
        return stats.queued == 0 && stats.dirty == 0 && stats.inFlight == 0 && stats.pendingCompleted == 0;
    });
    if (!settled) {
        fail("loaded area should settle before testing a single interior edit");
    }

    world.setBlock(8, 80, 8, BlockIds::STONE);
    const LightFrameStats stats = world.getLightFrameStats();
    if (stats.queued != 1 || stats.dirty != 1) {
        fail("interior block change should queue only the owning chunk");
    }
}

void testInteractiveFlushAppliesBlockLightBeforeNextWorldTick() {
    ThreadPool pool(2);
    pool.start();

    World world;
    ThreadPoolGuard poolGuard{pool};
    world.init(20260417);
    world.setRenderDistance(1);
    world.setThreadPool(&pool);

    const glm::vec3 playerPos(8.0f, 80.0f, 8.0f);
    tickWorld(world, playerPos, 10);

    const bool settled = waitUntil(world, playerPos, 240, [&]() {
        const LightFrameStats stats = world.getLightFrameStats();
        return stats.queued == 0 && stats.dirty == 0 && stats.inFlight == 0 && stats.pendingCompleted == 0;
    });
    if (!settled) {
        fail("loaded area should settle before testing interactive light flush");
    }

    constexpr int torchX = 8;
    constexpr int torchY = 81;
    constexpr int torchZ = 8;
    constexpr int sampleX = torchX + 1;
    world.setBlock(torchX, torchY - 1, torchZ, BlockIds::STONE);
    world.setBlock(torchX, torchY, torchZ, BlockIds::TORCH);

    const bool torchLit = waitUntil(world, playerPos, 120, [&]() {
        Chunk* chunk = findChunk(world, 0, 0);
        return chunk != nullptr && chunk->getBlockLight(sampleX, torchY, torchZ) > 0;
    });
    if (!torchLit) {
        fail("torch should light a neighboring voxel before removal");
    }

    const bool resettled = waitUntil(world, playerPos, 120, [&]() {
        const LightFrameStats stats = world.getLightFrameStats();
        return stats.queued == 0 && stats.dirty == 0 && stats.inFlight == 0 && stats.pendingCompleted == 0;
    });
    if (!resettled) {
        fail("torch placement should settle before testing immediate removal");
    }

    world.setBlock(torchX, torchY, torchZ, BlockIds::AIR);
    world.flushInteractiveLighting(playerPos);

    Chunk* chunk = findChunk(world, 0, 0);
    if (chunk == nullptr || chunk->getBlockLight(sampleX, torchY, torchZ) != 0) {
        fail("interactive light flush should apply block-light removal before the next world tick");
    }
}

void testBoundaryInboxDoesNotDirtyMeshBeforeLightApply() {
    ThreadPool pool(2);
    pool.start();

    World world;
    ThreadPoolGuard poolGuard{pool};
    world.init(20260417);
    world.setRenderDistance(1);
    world.setThreadPool(&pool);

    const glm::vec3 playerPos(8.0f, 80.0f, 8.0f);
    tickWorld(world, playerPos, 10);
    settleLoadedArea(world, playerPos);

    constexpr int y = 80;
    constexpr int z = 8;
    for (int x = 14; x <= 17; ++x) {
        world.setBlock(x, y, z, BlockIds::STONE);
    }
    tickWorld(world, playerPos, 40);
    const bool settled = waitUntil(world, playerPos, 240, [&]() {
        const LightFrameStats stats = world.getLightFrameStats();
        return stats.queued == 0 && stats.dirty == 0 && stats.inFlight == 0 && stats.pendingCompleted == 0;
    });
    if (!settled) {
        fail("tunnel setup should settle before testing boundary mesh dirtiness");
    }

    Chunk* rightChunk = findChunk(world, 1, 0);
    if (!rightChunk) {
        fail("expected +X neighbor chunk to stay loaded");
    }
    rightChunk->markMeshClean();

    world.setBlock(14, y + 1, z, BlockIds::TORCH);

    bool sawBoundarySync = false;
    for (int i = 0; i < 120; ++i) {
        world.update(playerPos);
        const LightFrameStats stats = world.getLightFrameStats();
        if (stats.boundarySync > 0 && rightChunk->getBlockLight(0, y + 1, z) == 0) {
            sawBoundarySync = true;
            if (rightChunk->isSubChunkDirty(Chunk::toSubChunkIndex(y + 1))) {
                fail("boundary inbox should not dirty neighbor mesh before neighbor light is applied");
            }
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (!sawBoundarySync) {
        fail("expected a boundary sync before the neighbor light apply");
    }
}
} // namespace

int main() {
    BlockRegistry::init(nullptr);

    testAdjacentChunksCanBeInFlightTogether();
    testUnloadDropsLateLightingResults();
    testHighFrequencyContinuousBlockChangesRequeueCleanly();
    testInteriorBlockChangeOnlyQueuesOwningChunk();
    testInteractiveFlushAppliesBlockLightBeforeNextWorldTick();
    testBoundaryInboxDoesNotDirtyMeshBeforeLightApply();

    pass();
}
