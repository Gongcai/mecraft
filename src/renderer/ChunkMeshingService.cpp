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
    std::queue<SubChunkMeshingResult> emptyResults;
    std::swap(m_completed, emptyResults);
}

void ChunkMeshingService::submit(SubChunkMeshingJob job, const int priority) {
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
        SubChunkMeshingResult result;
        result.chunkKey = job.chunkKey;
        result.scy = job.scy;
        result.revision = job.revision;

        if (job.chunk) {
            SubChunkMeshingSnapshotPtr snapshot = ChunkMesher::captureSubChunkSnapshot(
                *job.chunk, job.scy,
                job.neighborPosX.get(), job.neighborNegX.get(),
                job.neighborPosZ.get(), job.neighborNegZ.get(),
                job.world);
            if (snapshot) {
                result.meshData = ChunkMesher::buildSubChunkMeshData(*snapshot);

                // Offset bounds from sub-chunk local to column-local
                const int yBase = job.scy * SubChunk::SIZE;
                if (result.meshData.hasBounds) {
                    result.meshData.boundsMin.y += static_cast<float>(yBase);
                    result.meshData.boundsMax.y += static_cast<float>(yBase);
                }
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

void ChunkMeshingService::submit(ChunkMeshingJob job, const int priority) {
    // Legacy: split into per-sub-chunk jobs
    if (!job.chunk) return;

    for (int scy = 0; scy < Chunk::NUM_SUB_CHUNKS; ++scy) {
        if (ChunkMesher::shouldSkipSubChunk(*job.chunk, scy)) continue;

        SubChunkMeshingJob scJob;
        scJob.chunkKey = job.chunkKey;
        scJob.scy = scy;
        scJob.revision = job.chunk->getSubChunkMeshRevision(scy);
        scJob.chunk = job.chunk;
        scJob.neighborPosX = job.neighborPosX;
        scJob.neighborNegX = job.neighborNegX;
        scJob.neighborPosZ = job.neighborPosZ;
        scJob.neighborNegZ = job.neighborNegZ;
        scJob.world = job.world;
        submit(std::move(scJob), priority);
    }
}

bool ChunkMeshingService::tryPopCompleted(SubChunkMeshingResult& out) {
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
