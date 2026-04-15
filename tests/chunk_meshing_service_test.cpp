#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

#include "../src/renderer/ChunkMeshingService.h"
#include "../src/thread/ThreadPool.h"

namespace {
int fail(const char* message) {
    std::cerr << "[chunk_meshing_service_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

template <typename Predicate>
bool waitUntil(Predicate&& predicate, const int timeoutMs = 4000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return predicate();
}

ChunkMeshingJob makeDenseJob(const int64_t chunkKey,
                             const int chunkX,
                             const int chunkZ,
                             const int maxY,
                             const BlockID blockId = BlockIds::DIRT) {
    Chunk chunk(chunkX, chunkZ);
    for (int y = 0; y < maxY; ++y) {
        for (int z = 0; z < Chunk::SIZE_Z; ++z) {
            for (int x = 0; x < Chunk::SIZE_X; ++x) {
                chunk.setBlock(x, y, z, blockId);
            }
        }
    }

    ChunkMeshingJob job;
    job.chunkKey = chunkKey;
    job.revision = chunk.getMeshRevision();
    job.snapshot = ChunkMesher::captureSnapshot(chunk);
    return job;
}
}

int main() {
    BlockRegistry::init(nullptr);

    {
        ThreadPool pool(1);
        ChunkMeshingService service;
        std::vector<int64_t> completedKeys;

        pool.start();
        service.start(&pool);

        for (int i = 0; i < 8; ++i) {
            service.submit(makeDenseJob(100 + i, i, 0, 96), i);
        }

        if (service.inFlightCount() <= 0) {
            return fail("submitting jobs should increase the in-flight count");
        }

        if (!waitUntil([&]() {
                ChunkMeshingResult result;
                while (service.tryPopCompleted(result)) {
                    completedKeys.push_back(result.chunkKey);
                }
                return completedKeys.size() == 8 && service.inFlightCount() == 0;
            })) {
            return fail("all submitted meshing jobs should eventually complete");
        }

        std::sort(completedKeys.begin(), completedKeys.end());
        for (int i = 0; i < 8; ++i) {
            if (completedKeys[static_cast<size_t>(i)] != 100 + i) {
                return fail("completed results should contain every submitted chunk key");
            }
        }

        service.shutdown();
        pool.shutdown();
    }

    {
        ThreadPool pool(1);
        ChunkMeshingService service;

        pool.start();
        service.start(&pool);

        for (int i = 0; i < 8; ++i) {
            service.submit(makeDenseJob(200 + i, i, 1, 128), i);
        }

        service.shutdown();
        pool.shutdown();

        ChunkMeshingResult result;
        if (service.tryPopCompleted(result)) {
            return fail("shutdown service should not publish new completed results");
        }
        if (service.inFlightCount() != 0) {
            return fail("in-flight count should drain back to zero after the pool finishes");
        }

        service.submit(makeDenseJob(999, 0, 0, 64), 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (service.tryPopCompleted(result)) {
            return fail("service should reject submissions after shutdown");
        }
    }

    std::cout << "[chunk_meshing_service_test] PASS\n";
    return EXIT_SUCCESS;
}
