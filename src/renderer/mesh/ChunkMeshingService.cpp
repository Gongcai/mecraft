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

    std::lock_guard<SpinLock> recycledLock(m_recycledMeshDataLock);
    m_recycledMeshData.clear();
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

    pool->submit(
        [this, epoch, job = std::move(job)]() mutable {
            SubChunkMeshingResult result;
            result.chunkKey = job.chunkKey;
            result.scy = job.scy;
            result.revision = job.revision;

            if (job.snapshot) {
                result.meshData = acquireMeshData();
                ChunkMesher::buildSubChunkMeshData(*job.snapshot, result.meshData);
            }

            const bool shouldPublish =
                m_running.load(std::memory_order_acquire) && m_epoch.load(std::memory_order_acquire) == epoch;
            if (shouldPublish) {
                std::lock_guard<SpinLock> lock(m_completedLock);
                if (m_running.load(std::memory_order_acquire) && m_epoch.load(std::memory_order_acquire) == epoch) {
                    m_completed.push(std::move(result));
                    m_inFlight.fetch_sub(1, std::memory_order_release);
                    return;
                }
            }

            recycleMeshData(std::move(result.meshData));
            m_inFlight.fetch_sub(1, std::memory_order_release);
        },
        priority);
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

ChunkMeshData ChunkMeshingService::acquireMeshData() {
    std::lock_guard<SpinLock> lock(m_recycledMeshDataLock);
    if (m_recycledMeshData.empty()) {
        return {};
    }

    ChunkMeshData meshData = std::move(m_recycledMeshData.back());
    m_recycledMeshData.pop_back();
    return meshData;
}

void ChunkMeshingService::recycleMeshData(ChunkMeshData&& meshData) {
    meshData.opaqueVertices.clear();
    meshData.cutoutVertices.clear();
    meshData.cutoutDistanceVertices.clear();
    meshData.transparentVertices.clear();
    meshData.waterVertices.clear();
    meshData.opaqueFaceCountBeforeGreedy = 0;
    meshData.opaqueFaceCountAfterGreedy = 0;
    meshData.transparentFaceCountBeforeGreedy = 0;
    meshData.transparentFaceCountAfterGreedy = 0;
    meshData.opaqueVertexCount = 0;
    meshData.buildTimeMs = 0.0;
    meshData.hasBounds = false;
    meshData.boundsMin = glm::vec3(0.0f);
    meshData.boundsMax = glm::vec3(0.0f);

    std::lock_guard<SpinLock> lock(m_recycledMeshDataLock);
    m_recycledMeshData.push_back(std::move(meshData));
}

int ChunkMeshingService::inFlightCount() const {
    return m_inFlight.load(std::memory_order_acquire);
}
