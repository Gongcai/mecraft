#include "ChunkMeshingService.h"

#include <utility>

void ChunkMeshingService::start() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_running) {
        return;
    }

    m_stopping = false;
    m_running = true;
    m_worker = std::thread(&ChunkMeshingService::workerLoop, this);
}

void ChunkMeshingService::shutdown() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_running) {
            return;
        }
        m_stopping = true;
    }

    m_cv.notify_all();
    if (m_worker.joinable()) {
        m_worker.join();
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_running = false;

    std::queue<ChunkMeshingJob> emptyJobs;
    std::swap(m_pending, emptyJobs);

    std::queue<ChunkMeshingResult> emptyResults;
    std::swap(m_completed, emptyResults);
}

void ChunkMeshingService::submit(ChunkMeshingJob job) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_running || m_stopping) {
            return;
        }
        m_pending.push(std::move(job));
    }
    m_cv.notify_one();
}

bool ChunkMeshingService::tryPopCompleted(ChunkMeshingResult& out) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_completed.empty()) {
        return false;
    }

    out = std::move(m_completed.front());
    m_completed.pop();
    return true;
}

void ChunkMeshingService::workerLoop() {
    while (true) {
        ChunkMeshingJob job;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] { return m_stopping || !m_pending.empty(); });

            if (m_stopping && m_pending.empty()) {
                return;
            }

            job = std::move(m_pending.front());
            m_pending.pop();
        }

        if (job.atlas == nullptr) {
            continue;
        }

        ChunkMeshingResult result;
        result.chunkKey = job.chunkKey;
        result.revision = job.revision;
        result.meshData = ChunkMesher::buildMeshData(job.snapshot, *job.atlas);

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_stopping) {
                m_completed.push(std::move(result));
            }
        }
    }
}

