#ifndef MECRAFT_CHUNKMESHINGSERVICE_H
#define MECRAFT_CHUNKMESHINGSERVICE_H

#include <cstdint>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>

#include "ChunkMesher.h"

struct ChunkMeshingJob {
    int64_t chunkKey = 0;
    uint64_t revision = 0;
    ChunkMeshingSnapshot snapshot{};
    const TextureAtlas* atlas = nullptr;
};

struct ChunkMeshingResult {
    int64_t chunkKey = 0;
    uint64_t revision = 0;
    ChunkMeshData meshData;
};

class ChunkMeshingService {
public:
    ~ChunkMeshingService();
    void start();
    void shutdown();

    void submit(ChunkMeshingJob job);
    bool tryPopCompleted(ChunkMeshingResult& out);

private:
    void workerLoop();

    std::thread m_worker;
    std::mutex m_mutex;
    std::condition_variable m_cv;

    std::queue<ChunkMeshingJob> m_pending;
    std::queue<ChunkMeshingResult> m_completed;

    bool m_running = false;
    bool m_stopping = false;
    bool m_isShutdown = false;
};

#endif // MECRAFT_CHUNKMESHINGSERVICE_H

