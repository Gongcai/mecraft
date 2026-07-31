#ifndef MECRAFT_CHUNKMESHINGSERVICE_H
#define MECRAFT_CHUNKMESHINGSERVICE_H

#include <atomic>
#include <cstdint>
#include <memory>
#include <queue>

#include "ChunkMesher.h"
#include "../../thread/ThreadPool.h"

class World;

// Per-sub-chunk meshing job
struct SubChunkMeshingJob {
    int64_t chunkKey = 0;
    int scy = 0; // Sub-chunk index (0..15)
    uint64_t revision = 0; // Sub-chunk mesh revision for stale-check
    SubChunkMeshingSnapshotPtr snapshot;
};

// Per-sub-chunk meshing result
struct SubChunkMeshingResult {
    int64_t chunkKey = 0;
    int scy = 0;
    uint64_t revision = 0;
    ChunkMeshData meshData;
};

class ChunkMeshingService {

public:
    void start(ThreadPool* pool);
    void shutdown();

    // Per-sub-chunk submission
    void submit(SubChunkMeshingJob job, int priority);
    bool tryPopCompleted(SubChunkMeshingResult& out);
    void recycleMeshData(ChunkMeshData&& meshData);

    [[nodiscard]] int inFlightCount() const;

private:
    ChunkMeshData acquireMeshData();

    mutable std::mutex m_stateMutex;
    SpinLock m_completedLock;
    SpinLock m_recycledMeshDataLock;
    ThreadPool* m_pool = nullptr;
    std::queue<SubChunkMeshingResult> m_completed;
    std::vector<ChunkMeshData> m_recycledMeshData;
    std::atomic<int> m_inFlight{0};
    std::atomic<uint64_t> m_epoch{0};
    std::atomic<bool> m_running{false};
};

#endif // MECRAFT_CHUNKMESHINGSERVICE_H
