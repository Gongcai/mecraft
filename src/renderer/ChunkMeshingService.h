#ifndef MECRAFT_CHUNKMESHINGSERVICE_H
#define MECRAFT_CHUNKMESHINGSERVICE_H

#include <atomic>
#include <cstdint>
#include <memory>
#include <queue>

#include "ChunkMesher.h"
#include "../thread/ThreadPool.h"

class World;

struct ChunkMeshingJob {
    int64_t chunkKey = 0;
    uint64_t revision = 0;
    // Chunk reference kept alive for async snapshot capture on worker thread
    std::shared_ptr<Chunk> chunk;
    // Neighbor references so captureBorders can safely read border data
    std::shared_ptr<Chunk> neighborPosX;   // neighbors[0]
    std::shared_ptr<Chunk> neighborNegX;   // neighbors[1]
    std::shared_ptr<Chunk> neighborPosZ;   // neighbors[2]
    std::shared_ptr<Chunk> neighborNegZ;   // neighbors[3]
    const World* world = nullptr;
};

struct ChunkMeshingResult {
    int64_t chunkKey = 0;
    uint64_t revision = 0;
    ChunkMeshData meshData;
};

class ChunkMeshingService {
public:
    void start(ThreadPool* pool);
    void shutdown();

    void submit(ChunkMeshingJob job, int priority);
    bool tryPopCompleted(ChunkMeshingResult& out);
    [[nodiscard]] int inFlightCount() const;

private:
    mutable std::mutex m_stateMutex;
    SpinLock m_completedLock;   // hot-path: workers push, main thread pops
    ThreadPool* m_pool = nullptr;
    std::queue<ChunkMeshingResult> m_completed;
    std::atomic<int> m_inFlight{0};
    std::atomic<uint64_t> m_epoch{0};
    std::atomic<bool> m_running{false};
};

#endif // MECRAFT_CHUNKMESHINGSERVICE_H

