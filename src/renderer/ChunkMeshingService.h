#ifndef MECRAFT_CHUNKMESHINGSERVICE_H
#define MECRAFT_CHUNKMESHINGSERVICE_H

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <queue>

#include "ChunkMesher.h"
#include "../thread/ThreadPool.h"

struct ChunkMeshingJob {
    int64_t chunkKey = 0;
    uint64_t revision = 0;
    ChunkMeshingSnapshotPtr snapshot;
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
    std::mutex m_completedMutex;
    ThreadPool* m_pool = nullptr;
    std::queue<ChunkMeshingResult> m_completed;
    std::atomic<int> m_inFlight{0};
    std::atomic<uint64_t> m_epoch{0};
    std::atomic<bool> m_running{false};
};

#endif // MECRAFT_CHUNKMESHINGSERVICE_H

