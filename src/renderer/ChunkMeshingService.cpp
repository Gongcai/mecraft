#include "ChunkMeshingService.h"

#include <utility>

void ChunkMeshingService::start(ThreadPool* pool) {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    if (m_running.load(std::memory_order_acquire) || pool == nullptr) {
        return;
    }

    m_pool = pool;
    m_running.store(true, std::memory_order_release);
    m_epoch.fetch_add(1, std::memory_order_release);
}

void ChunkMeshingService::shutdown() {
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        if (!m_running.load(std::memory_order_acquire)) {
            return;
        }
        m_running.store(false, std::memory_order_release);
        m_pool = nullptr;
        m_epoch.fetch_add(1, std::memory_order_release);
    }

    std::lock_guard<SpinLock> completedLock(m_completedLock);
    std::queue<ChunkMeshingResult> emptyResults;
    std::swap(m_completed, emptyResults);
}

void ChunkMeshingService::submit(ChunkMeshingJob job, const int priority) {
    ThreadPool* pool = nullptr;
    uint64_t epoch = 0;
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        if (!m_running.load(std::memory_order_acquire) || m_pool == nullptr) {
            return;
        }
        pool = m_pool;
        epoch = m_epoch.load(std::memory_order_acquire);
        m_inFlight.fetch_add(1, std::memory_order_relaxed);
    }

    pool->submit([this, epoch, job = std::move(job)]() mutable {
        ChunkMeshingResult result;
        result.chunkKey = job.chunkKey;
        result.revision = job.revision;

        if (job.chunk) {
            ChunkMeshingSnapshotPtr snapshot = ChunkMesher::captureSnapshot(
                *job.chunk, job.neighborPosX.get(), job.neighborNegX.get(),
                job.neighborPosZ.get(), job.neighborNegZ.get(),
                job.world);
            if (snapshot) {
                result.meshData = ChunkMesher::buildMeshData(*snapshot);
            }
        }

        const bool shouldPublish = m_running.load(std::memory_order_acquire) &&
            m_epoch.load(std::memory_order_acquire) == epoch;
        if (shouldPublish) {
            std::lock_guard<SpinLock> lock(m_completedLock);
            if (m_running.load(std::memory_order_acquire) &&
                m_epoch.load(std::memory_order_acquire) == epoch) {
                m_completed.push(std::move(result));
            }
        }

        m_inFlight.fetch_sub(1, std::memory_order_release);
    }, priority);
}

bool ChunkMeshingService::tryPopCompleted(ChunkMeshingResult& out) {
    std::lock_guard<SpinLock> lock(m_completedLock);
    if (m_completed.empty()) {
        return false;
    }

    out = std::move(m_completed.front());
    m_completed.pop();
    return true;
}

int ChunkMeshingService::inFlightCount() const {
    return m_inFlight.load(std::memory_order_acquire);
}

