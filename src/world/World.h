#ifndef MECRAFT_WORLD_H
#define MECRAFT_WORLD_H

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <glm/glm.hpp>

#include "block/BlockNeighborUpdateQueue.h"
#include "chunk/Chunk.h"
#include "DayNightSystem.h"
#include "../server/ChunkTicketManager.h"
#include "../save/ChunkSerializer.h"
#include "fluid/FluidState.h"
#include "fluid/FluidSystem.h"
#include "redstone/WireContainerParts.h"
#include "redstone/RedstoneRuntimeState.h"
#include "redstone/RedstoneSimulator.h"
#include "redstone/RedstoneUpdateQueue.h"
#include "WeatherSystem.h"
#include "light/LightService.h"
#include "gen/TerrainGenerator.h"
#include "../physics/PhysicsInfo.h"
#include "../thread/ThreadPool.h"
#include "IWorldView.h"

namespace save {
class SaveManager;
}

class World : public IWorldView {
public:
    struct ChunkLoadProgress {
        int loaded = 0;
        int target = 0;
        int inFlight = 0;
    };

    void init(uint32_t seed);
    void update(const glm::vec3& playerPos, float dt = 1.0f / 60.0f);
    void updateForInitialLoad(const glm::vec3& playerPos, float dt);
    void flushInteractiveLighting(const glm::vec3& playerPos);

    [[nodiscard]] uint32_t getSeed() const { return m_seed; }

    [[nodiscard]] BlockStateId getBlock(int x, int y, int z) const override;
    [[nodiscard]] uint8_t getPackedLight(int x, int y, int z) const override;
    [[nodiscard]] BlockStateId getBlockState(int x, int y, int z) const override;
    [[nodiscard]] BlockStateId getFluidState(int x, int y, int z) const override;
    [[nodiscard]] FluidCellView getCombinedCell(int x, int y, int z) const;
    [[nodiscard]] BlockStateId sampleGeneratedBlock(int x, int y, int z) const;
    void setBlock(int x, int y, int z, BlockID id);
    void setBlockState(int x, int y, int z, BlockStateId stateId);
    void setFluidState(int x, int y, int z, BlockStateId stateId);
    [[nodiscard]] bool isChunkLoadedForBlock(int x, int y, int z) const override;

    [[nodiscard]] RayHit raycast(const PhysicsInfo& ray, float maxDist) const;
    bool raycast(const PhysicsInfo& ray, float maxDist, glm::ivec3& hitBlock, glm::ivec3& placeBlock) const;

    [[nodiscard]] const ChunkMap& getActiveChunks() const override { return m_chunks; }
    [[nodiscard]] uint64_t getActiveChunkRevision() const override { return m_activeChunkRevision; }
    [[nodiscard]] uint64_t getBlockContentRevision() const override { return m_blockContentRevision; }
    [[nodiscard]] uint64_t getBlockEditRevision() const override { return m_blockEditRevision; }
    void setThreadPool(ThreadPool* pool);
    [[nodiscard]] LightFrameStats getLightFrameStats() const;

    [[nodiscard]] size_t getTotalVertexCount() const;

    [[nodiscard]] int getRenderDistance() const override { return m_renderDistance; }
    void setRenderDistance(int dist);

    [[nodiscard]] int getFlatSurfaceY() const { return m_flatSurfaceY; }
    [[nodiscard]] int getSurfaceY(int x, int z) const;
    [[nodiscard]] TerrainBiome getBiome(int x, int z) const override;
    [[nodiscard]] glm::ivec2 getChunkCoords(int worldX, int worldZ) const override;
    [[nodiscard]] ChunkLoadProgress getChunkLoadProgress(const glm::vec3& center) const;
    [[nodiscard]] const World* asWorld() const override { return this; }
    [[nodiscard]] bool copyWireContainerParts(const glm::ivec3& position, WireContainerParts& out) const override {
        const WireContainerParts* parts = m_wireContainerParts.find(position);
        if (parts == nullptr) {
            return false;
        }
        out = *parts;
        return true;
    }
    [[nodiscard]] static const char* biomeToString(TerrainBiome biome);

    static int64_t chunkKey(int cx, int cz);

    DayNightSystem& getDayNightSystem() { return m_dayNightSystem; }
    const DayNightSystem& getDayNightSystem() const { return m_dayNightSystem; }
    FluidSystem& fluidSystem() { return m_fluidSystem; }
    const FluidSystem& fluidSystem() const { return m_fluidSystem; }
    WeatherSystem& getWeatherSystem() { return m_weatherSystem; }
    const WeatherSystem& getWeatherSystem() const { return m_weatherSystem; }

    BlockNeighborUpdateQueue& neighborUpdateQueue() { return m_neighborUpdateQueue; }
    const BlockNeighborUpdateQueue& neighborUpdateQueue() const { return m_neighborUpdateQueue; }
    BlockNeighborUpdateQueue& redstoneUpdateQueue() { return m_redstoneUpdateQueue; }
    const BlockNeighborUpdateQueue& redstoneUpdateQueue() const { return m_redstoneUpdateQueue; }
    BlockNeighborUpdateQueue& redstoneChangedBlockQueue() { return m_redstoneChangedBlockQueue; }
    const BlockNeighborUpdateQueue& redstoneChangedBlockQueue() const { return m_redstoneChangedBlockQueue; }
    RedstoneUpdateQueue& redstoneScheduledUpdateQueue() { return m_redstoneScheduledUpdateQueue; }
    const RedstoneUpdateQueue& redstoneScheduledUpdateQueue() const { return m_redstoneScheduledUpdateQueue; }
    RedstoneRuntimeState& redstoneRuntimeState() { return m_redstoneRuntimeState; }
    const RedstoneRuntimeState& redstoneRuntimeState() const { return m_redstoneRuntimeState; }
    WireContainerPartStore& wireContainerParts() { return m_wireContainerParts; }
    const WireContainerPartStore& wireContainerParts() const { return m_wireContainerParts; }
    RedstoneSimulator& redstoneSimulator() { return m_redstoneSimulator; }
    const RedstoneSimulator& redstoneSimulator() const { return m_redstoneSimulator; }
    void notifyWireContainerPartsChanged(const glm::ivec3& pos);
    void setLastProcessedRedstoneTick(uint64_t redstoneTick) { m_lastProcessedRedstoneTick = redstoneTick; }
    [[nodiscard]] uint64_t lastProcessedRedstoneTick() const { return m_lastProcessedRedstoneTick; }

    /// Access the chunk ticket manager (for GameServer per-client management).
    ChunkTicketManager& ticketManager() { return m_ticketManager; }
    const ChunkTicketManager& ticketManager() const { return m_ticketManager; }

    /// Set simulation distance (entity/fluid/random tick radius).
    void setSimulationDistance(int distance);

    /// Callback invoked after any block state change (setBlockState / setFluidState).
    /// Used by GameServer to collect dirty blocks for BlockUpdateBatch messages.
    using BlockChangeCallback = std::function<void(int x, int y, int z, BlockStateId newStateId)>;
    void setBlockChangeCallback(BlockChangeCallback callback) { m_blockChangeCallback = std::move(callback); }
    using WireContainerChangeCallback = std::function<void(const glm::ivec3& pos)>;
    void setWireContainerChangeCallback(WireContainerChangeCallback callback) {
        m_wireContainerChangeCallback = std::move(callback);
    }

    // --- Save system integration ---

    /// Bind a SaveManager for chunk persistence. Null to disable.
    void setSaveManager(save::SaveManager* saveManager);

    /// Mark a chunk as needing to be saved (dirty for persistence).
    void markChunkSaveDirty(int cx, int cz);

    /// Flush all dirty chunks to disk and wait for completion.
    /// Called during shutdown before destroying the world.
    void flushSaves();

    /// Callback invoked after async lighting has merged changed sub-chunks.
    using LightChangeCallback = LightService::LightChangeCallback;
    void setLightChangeCallback(LightChangeCallback callback);

private:
    std::unordered_map<int64_t, std::shared_ptr<Chunk>> m_chunks;

    TerrainGenerator m_terrainGen;
    std::unique_ptr<LightService> m_lightService;
    DayNightSystem m_dayNightSystem;
    FluidSystem m_fluidSystem{*this};
    RedstoneSimulator m_redstoneSimulator{*this};
    WeatherSystem m_weatherSystem;
    BlockNeighborUpdateQueue m_neighborUpdateQueue;
    BlockNeighborUpdateQueue m_redstoneUpdateQueue;
    BlockNeighborUpdateQueue m_redstoneChangedBlockQueue;
    RedstoneUpdateQueue m_redstoneScheduledUpdateQueue;
    RedstoneRuntimeState m_redstoneRuntimeState;
    WireContainerPartStore m_wireContainerParts;
    uint64_t m_lastProcessedRedstoneTick = 0;
    ThreadPool* m_threadPool = nullptr;
    BlockChangeCallback m_blockChangeCallback;
    WireContainerChangeCallback m_wireContainerChangeCallback;
    LightChangeCallback m_lightChangeCallback;
    ChunkTicketManager m_ticketManager;

    // Save system
    save::SaveManager* m_saveManager = nullptr;
    std::unordered_set<int64_t> m_dirtySaveChunks;

    int m_renderDistance = 8;
    uint32_t m_seed = 0;
    int m_flatSurfaceY = 63;
    uint64_t m_activeChunkRevision = 1;
    uint64_t m_blockContentRevision = 1;
    // Advances only for in-place block edits; chunk streaming never bumps it.
    uint64_t m_blockEditRevision = 1;
    bool m_interactiveLightFlushRequested = false;

    void loadChunk(int cx, int cz);
    void unloadChunk(int cx, int cz);
    void refreshConnectedBlockAt(const glm::ivec3& pos);
    void refreshConnectedBlocksAround(const glm::ivec3& pos);
    void submitChunkLoad(int cx, int cz);
    void finalizeChunkLoad(save::ChunkLoadData loadData);
    [[nodiscard]] std::vector<save::WireContainerSaveEntry> collectWireContainersForChunk(int cx, int cz) const;
    void applyLoadedWireContainers(int cx, int cz, const std::vector<save::WireContainerSaveEntry>& wireContainers);
    void eraseWireContainersInChunk(int cx, int cz);
    void updateStreaming(const glm::vec3& playerPos, float dt, int submitBudget, int maxGenerationInFlight,
                         int finalizeBudget, double finalizeTimeBudgetMs, int lightSubmitBudget, int lightMergeBudget,
                         float lightMergeTimeBudgetMs);

    std::vector<glm::ivec2> m_loadQueue;
    void updateLoadQueue(int playerChunkX, int playerChunkZ);

    std::unordered_set<int64_t> m_generationInFlight;
    std::mutex m_completedGenMutex;
    std::vector<save::ChunkLoadData> m_completedGenQueue;

    static constexpr int kMaxGenerationInFlight = 3;
    static constexpr int kMaxChunkLoadSubmitsPerFrame = 2;
    static constexpr int kMaxChunkLoadFinalizesPerFrame = 1;
    static constexpr double kChunkLoadFinalizeTimeBudgetMs = 1.0;
};

#endif //MECRAFT_WORLD_H
