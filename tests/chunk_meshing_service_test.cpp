#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

#include "../src/renderer/mesh/ChunkMeshingService.h"
#include "../src/thread/ThreadPool.h"

namespace {
int fail(const char* message) {
    std::cerr << "[chunk_meshing_service_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

BlockStateId stateForBlockId(const BlockID blockId) {
    return blockId == RUNTIME_ID_NULL
        ? NULL_BLOCK_STATE
        : BlockStateRegistry::getDefaultState(blockId);
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

SubChunkMeshingJob makeDenseJob(const int64_t chunkKey,
                                const int chunkX,
                                const int chunkZ,
                                const int scy,
                                const BlockID blockId = BlockRegistry::requireIdByName("minecraft:dirt")) {
    auto chunkPtr = std::make_shared<Chunk>(chunkX, chunkZ);
    const int yBase = scy * SubChunk::SIZE;
    for (int y = 0; y < SubChunk::SIZE; ++y) {
        for (int z = 0; z < Chunk::SIZE_Z; ++z) {
            for (int x = 0; x < Chunk::SIZE_X; ++x) {
                chunkPtr->setBlock(x, yBase + y, z, stateForBlockId(blockId));
            }
        }
    }

    SubChunkMeshingJob job;
    job.chunkKey = chunkKey;
    job.scy = scy;
    job.revision = chunkPtr->getSubChunkMeshRevision(scy);
    job.snapshot = ChunkMesher::captureSubChunkSnapshot(*chunkPtr, scy);
    return job;
}
} // namespace

int main() {
    BlockRegistry::init(nullptr);

    {
        ThreadPool pool(1);
        ChunkMeshingService service;
        std::vector<int64_t> completedKeys;

        pool.start();
        service.start(&pool);

        for (int i = 0; i < 8; ++i) {
            service.submit(makeDenseJob(100 + i, i, 0, i % 3), i);
        }

        if (service.inFlightCount() <= 0) {
            return fail("submitting jobs should increase the in-flight count");
        }

        if (!waitUntil([&]() {
                SubChunkMeshingResult result;
                while (service.tryPopCompleted(result)) {
                    completedKeys.push_back(result.chunkKey);
                }
                return completedKeys.size() >= 8 && service.inFlightCount() == 0;
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
            service.submit(makeDenseJob(200 + i, i, 1, (i % 4) + 1), i);
        }

        service.shutdown();
        pool.shutdown();

        SubChunkMeshingResult result;
        if (service.tryPopCompleted(result)) {
            return fail("shutdown service should not publish new completed results");
        }
        if (service.inFlightCount() != 0) {
            return fail("in-flight count should drain back to zero after the pool finishes");
        }

        service.submit(makeDenseJob(999, 0, 0, 0), 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        SubChunkMeshingResult result2;
        if (service.tryPopCompleted(result2)) {
            return fail("service should reject submissions after shutdown");
        }
    }

    std::cout << "[chunk_meshing_service_test] PASS\n";
    return EXIT_SUCCESS;
}
