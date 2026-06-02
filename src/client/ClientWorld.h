#ifndef MECRAFT_CLIENT_WORLD_H
#define MECRAFT_CLIENT_WORLD_H

#include "../world/IWorldView.h"
#include "../world/chunk/Chunk.h"
#include "../world/DayNightSystem.h"
#include "../world/WeatherSystem.h"
#include <unordered_map>
#include <memory>
#include <mutex>

namespace client {

/// Client-side world mirror. Receives chunk data from the server
/// and provides a read-only IWorldView for the renderer and gameplay queries.
class ClientWorld : public IWorldView {
public:
    ClientWorld();
    ~ClientWorld();

    // --- IWorldView implementation ---
    [[nodiscard]] const ChunkMap& getActiveChunks() const override;
    [[nodiscard]] uint64_t getActiveChunkRevision() const override;
    [[nodiscard]] BlockID getBlock(int x, int y, int z) const override;
    [[nodiscard]] uint8_t getPackedLight(int x, int y, int z) const override;
    [[nodiscard]] StateID getBlockState(int x, int y, int z) const override;
    [[nodiscard]] StateID getFluidState(int x, int y, int z) const override;
    [[nodiscard]] bool isChunkLoadedForBlock(int x, int y, int z) const override;
    [[nodiscard]] int getRenderDistance() const override;
    [[nodiscard]] glm::ivec2 getChunkCoords(int worldX, int worldZ) const override;
    [[nodiscard]] TerrainBiome getBiome(int x, int z) const override;

    // ClientWorld is not backed by a server World.
    [[nodiscard]] const World* asWorld() const override { return nullptr; }

    // --- Chunk management (called by GameClient when receiving server messages) ---

    /// Install a chunk received from the server.
    void addChunk(std::shared_ptr<Chunk> chunk);

    /// Remove a chunk (server sent ChunkUnload).
    void removeChunk(int cx, int cz);

    /// Apply a block update from the server.
    void applyBlockUpdate(int x, int y, int z, BlockID blockId);

    /// Set the render distance.
    void setRenderDistance(int distance);

    /// Get the number of loaded chunks.
    [[nodiscard]] size_t loadedChunkCount() const;

    // --- Weather / DayNight proxy (in-process mode) ---
    // These non-owning pointers let the renderer access server-side systems
    // when operating in-process. In Phase 6 (real networking), these will be
    // replaced by data received from the server.

    void setDayNightSystem(const DayNightSystem* dns) { m_dayNightSystem = dns; }
    void setWeatherSystem(const WeatherSystem* ws) { m_weatherSystem = ws; }
    [[nodiscard]] const DayNightSystem* dayNightSystem() const { return m_dayNightSystem; }
    [[nodiscard]] const WeatherSystem* weatherSystem() const { return m_weatherSystem; }

private:
    ChunkMap m_chunks;
    mutable std::mutex m_chunksMutex;
    uint64_t m_activeChunkRevision = 1;
    int m_renderDistance = 16;

    // Non-owning pointers to server systems (in-process mode only)
    const DayNightSystem* m_dayNightSystem = nullptr;
    const WeatherSystem* m_weatherSystem = nullptr;
};

} // namespace client

#endif // MECRAFT_CLIENT_WORLD_H
