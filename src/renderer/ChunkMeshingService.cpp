#include "ChunkMeshingService.h"

#include <utility>

void ChunkMeshingService::start(ThreadPool* pool) {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    if (m_running.load(std::memory_order_relaxed) || pool == nullptr) {
        return;
    }

    m_pool = pool;
    m_running.store(true, std::memory_order_relaxed);
    m_epoch.fetch_add(1, std::memory_order_relaxed);
}

void ChunkMeshingService::shutdown() {
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        if (!m_running.load(std::memory_order_relaxed)) {
            return;
        }
        m_running.store(false, std::memory_order_relaxed);
        m_pool = nullptr;
        m_epoch.fetch_add(1, std::memory_order_relaxed);
    }

    std::lock_guard<std::mutex> completedLock(m_completedMutex);
    std::queue<ChunkMeshingResult> emptyResults;
    std::swap(m_completed, emptyResults);
}

void ChunkMeshingService::submit(ChunkMeshingJob job, const int priority) {
    ThreadPool* pool = nullptr;
    uint64_t epoch = 0;
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        if (!m_running.load(std::memory_order_relaxed) || m_pool == nullptr) {
            return;
        }
        pool = m_pool;
        epoch = m_epoch.load(std::memory_order_relaxed);
        m_inFlight.fetch_add(1, std::memory_order_relaxed);
    }

    pool->submit([this, epoch, job = std::move(job)]() mutable {
        ChunkMeshingResult result;
        result.chunkKey = job.chunkKey;
        result.revision = job.revision;
        if (job.snapshot) {
            result.meshData = ChunkMesher::buildMeshData(*job.snapshot);
        }

        const bool shouldPublish = m_running.load(std::memory_order_relaxed) &&
            m_epoch.load(std::memory_order_relaxed) == epoch;
        if (shouldPublish) {
            std::lock_guard<std::mutex> lock(m_completedMutex);
            if (m_running.load(std::memory_order_relaxed) &&
                m_epoch.load(std::memory_order_relaxed) == epoch) {
                m_completed.push(std::move(result));
            }
        }

        m_inFlight.fetch_sub(1, std::memory_order_relaxed);
    }, priority);
}

bool ChunkMeshingService::tryPopCompleted(ChunkMeshingResult& out) {
    std::lock_guard<std::mutex> lock(m_completedMutex);
    if (m_completed.empty()) {
        return false;
    }

    out = std::move(m_completed.front());
    m_completed.pop();
    return true;
}

int ChunkMeshingService::inFlightCount() const {
    return m_inFlight.load(std::memory_order_relaxed);
}

