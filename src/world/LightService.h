#ifndef MECRAFT_LIGHTSERVICE_H
#define MECRAFT_LIGHTSERVICE_H

#include <array>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <unordered_map>
#include <vector>

#include <glm/vec3.hpp>

#include "LightTypes.h"

class Chunk;
class ThreadPool;
class World;

class LightService {
public:
    explicit LightService(World& world);
    ~LightService();

    void start(ThreadPool* pool);
    void shutdown();

    void onChunkLoaded(const std::shared_ptr<Chunk>& chunk);
    void onChunkUnloaded(int64_t chunkKey);
    void onBlockChanged(int wx, int wy, int wz, BlockID oldId, BlockID newId);

    void submitJobs(const glm::vec3& cameraPos, int submitBudget);
    void drainCompleted(World& world, int mergeBudget = 32);

    [[nodiscard]] LightFrameStats getFrameStats() const;

private:
    struct LightChunkState {
        bool dirty = false;
        bool queued = false;
        bool inFlight = false;
        uint64_t inFlightRevision = 0;
        std::vector<LocalLightChange> pendingBlockChanges;
        std::array<std::optional<BorderUpdateBatch>, 4> boundaryCache;
        std::array<std::optional<BorderUpdateBatch>, 4> pendingPreviousBoundaryCache;
        std::array<bool, 4> pendingBoundaryChanged{};
        LightDirtyReason reason = LightDirtyReason::NeighborBoundary;
    };

    struct CompletedTicket {
        LightResult result;
    };

    static std::vector<BlockID> captureBlockSnapshot(const Chunk& chunk);
    static std::vector<int16_t> captureHeightMapSnapshot(const Chunk& chunk);
    static std::vector<uint8_t> capturePackedLightSnapshot(const Chunk& chunk);
    static std::vector<BorderUpdateBatch> collectBoundaryInputs(const LightChunkState& state);
    static std::vector<BorderUpdateBatch> collectBoundaryInputs(
        const std::array<std::optional<BorderUpdateBatch>, 4>& cache);
    static void clearBoundaryInputs(std::array<std::optional<BorderUpdateBatch>, 4>& cache);
    void markChunkDirty(int64_t chunkKey, LightDirtyReason reason);
    void onWorkerCompleted(CompletedTicket ticket);

    World& m_world;
    ThreadPool* m_pool = nullptr;
    LightFrameStats m_frameStats{};
    std::unordered_map<int64_t, LightChunkState> m_chunkStates;
    std::queue<CompletedTicket> m_completed;
    std::mutex m_stateMutex;
    std::mutex m_completedMutex;
    bool m_running = false;
};

#endif // MECRAFT_LIGHTSERVICE_H






