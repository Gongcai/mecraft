#ifndef MECRAFT_LIGHTSERVICE_H
#define MECRAFT_LIGHTSERVICE_H

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <deque>
#include <unordered_set>
#include <unordered_map>
#include <vector>

#include <glm/vec3.hpp>

#include "LightTypes.h"
#include "LightCache.h"
#include "../block/BlockStateRegistry.h"

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
    void onBlockChanged(int wx, int wy, int wz, BlockStateId oldStateId, BlockStateId newStateId);

    void submitJobs(const glm::vec3& cameraPos, int submitBudget);
    void drainCompleted(World& world, int mergeBudget = 32, float timeBudgetMs = 1.0f);
    int processInteractiveJobsInline(const glm::vec3& cameraPos, int jobBudget, int mergeBudget = 8,
                                     float mergeTimeBudgetMs = 2.0f);

    using LightChangeCallback = std::function<void(int64_t chunkKey, uint32_t dirtySubChunkMask)>;
    void setLightChangeCallback(LightChangeCallback callback) { m_lightChangeCallback = std::move(callback); }

    [[nodiscard]] LightFrameStats getFrameStats() const;
    [[nodiscard]] int countDirtyChunks() const;
    [[nodiscard]] int countPendingInteractiveJobs() const;
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
        uint32_t pendingHaloMeshDirtyMask = 0;
        uint32_t inFlightHaloMeshDirtyMask = 0;
        uint8_t pendingForceOutgoingBoundaryMask = 0;
        uint8_t inFlightForceOutgoingBoundaryMask = 0;
        LightDirtyReason reason = LightDirtyReason::NeighborBoundary;
        LightDirtyReason lastSubmitReason = LightDirtyReason::NeighborBoundary;
    };

    struct CompletedTicket {
        LightResult result;
    };

    static bool isInteractiveReason(LightDirtyReason reason);
    static std::vector<BlockID> captureBlockSnapshot(const Chunk& chunk);
    static std::vector<uint8_t> capturePackedLightSnapshot(const Chunk& chunk);
    static std::vector<BorderUpdateBatch> collectBoundaryInputs(const LightChunkState& state);
    static std::vector<BorderUpdateBatch>
    collectBoundaryInputs(const std::array<std::optional<BorderUpdateBatch>, 4>& cache);
    static void clearBoundaryInputs(std::array<std::optional<BorderUpdateBatch>, 4>& cache);
    void markChunkDirty(int64_t chunkKey, LightDirtyReason reason);
    void onWorkerCompleted(CompletedTicket ticket);

    // Base-light cache — incrementally maintained on the main thread so that
    // workers never pay the full buildCurrentBasePacked() cost.
    void ensureBaseLightCache(const std::shared_ptr<Chunk>& chunk);
    void invalidateBaseLightCache(int64_t chunkKey);
    void updateBaseLightCacheForBlockChange(const Chunk& chunk, int localX, int y, int localZ, BlockID oldId,
                                            BlockID newId);

    World& m_world;
    ThreadPool* m_pool = nullptr;
    LightFrameStats m_frameStats{};
    std::unordered_map<int64_t, LightChunkState> m_chunkStates;
    std::unordered_map<int64_t, CachedBaseLight> m_baseLightCaches;
    std::unordered_set<int64_t> m_frameBlockChangedChunks;
    std::deque<CompletedTicket> m_completed;
    LightChangeCallback m_lightChangeCallback;
    mutable std::mutex m_stateMutex;
    mutable std::mutex m_completedMutex;
    std::atomic<int> m_completedCount{0};
    bool m_running = false;
};

#endif // MECRAFT_LIGHTSERVICE_H
