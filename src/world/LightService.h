#ifndef MECRAFT_LIGHTSERVICE_H
#define MECRAFT_LIGHTSERVICE_H

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <unordered_map>
#include <vector>

#include <glm/vec3.hpp>

#include "LightTypes.h"
#include "LightCache.h"

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
    [[nodiscard]] int countDirtyChunks() const;
    [[nodiscard]] int completedCount() const { return m_completedCount.load(); }

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
        LightDirtyReason lastSubmitReason = LightDirtyReason::NeighborBoundary;
    };

    struct CompletedTicket {
        LightResult result;
    };

    static std::vector<BlockID> captureBlockSnapshot(const Chunk& chunk);
    static std::vector<uint8_t> capturePackedLightSnapshot(const Chunk& chunk);
    static std::vector<BorderUpdateBatch> collectBoundaryInputs(const LightChunkState& state);
    static std::vector<BorderUpdateBatch> collectBoundaryInputs(
        const std::array<std::optional<BorderUpdateBatch>, 4>& cache);
    static void clearBoundaryInputs(std::array<std::optional<BorderUpdateBatch>, 4>& cache);
    void markChunkDirty(int64_t chunkKey, LightDirtyReason reason);
    void onWorkerCompleted(CompletedTicket ticket);

    // Base-light cache — incrementally maintained on the main thread so that
    // workers never pay the full buildCurrentBasePacked() cost.
    void ensureBaseLightCache(const std::shared_ptr<Chunk>& chunk);
    void invalidateBaseLightCache(int64_t chunkKey);
    void updateBaseLightCacheForBlockChange(const Chunk& chunk,
                                            int localX, int y, int localZ,
                                            BlockID oldId, BlockID newId);

    World& m_world;
    ThreadPool* m_pool = nullptr;
    LightFrameStats m_frameStats{};
    std::unordered_map<int64_t, LightChunkState> m_chunkStates;
    std::unordered_map<int64_t, CachedBaseLight> m_baseLightCaches;
    std::queue<CompletedTicket> m_completed;
    mutable std::mutex m_stateMutex;
    mutable std::mutex m_completedMutex;
    std::atomic<int> m_completedCount{0};
    bool m_running = false;
};

#endif // MECRAFT_LIGHTSERVICE_H






