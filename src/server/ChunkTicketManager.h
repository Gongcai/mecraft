#ifndef MECRAFT_CHUNK_TICKET_MANAGER_H
#define MECRAFT_CHUNK_TICKET_MANAGER_H

#include <cstdint>
#include <vector>
#include <unordered_set>
#include <glm/glm.hpp>

/// Chunk ticket types with different priorities and radii.
enum class ChunkTicketType : uint8_t {
    Spawn, // World spawn area — always loaded
    PlayerSimulation, // Player position — ticking radius
    PlayerView, // Player view distance — loaded, sent to client
    Forced, // Command/debug — always loaded
};

/// Manages chunk loading tickets with multiple radii and unload hysteresis.
/// Used by both World (for local chunk management) and GameServer (for per-client streaming).
class ChunkTicketManager {
public:
    ChunkTicketManager();

    /// Reset all tickets and state.
    void reset();

    /// Set the simulation radius (entity/fluid/random tick distance).
    void setSimulationRadius(int radius) { m_simulationRadius = radius; }

    /// Set the server view distance (chunks loaded and sent to clients).
    void setViewRadius(int radius) { m_viewRadius = radius; }

    /// Update the player's chunk position for simulation and view tickets.
    void updatePlayerPosition(int chunkX, int chunkZ);

    /// Add a spawn ticket at the given chunk position.
    void addSpawnTicket(int chunkX, int chunkZ);

    /// Check if a chunk should be ticking (within simulation radius).
    [[nodiscard]] bool shouldTick(int cx, int cz) const;

    /// Check if a chunk should be loaded (within load radius = viewRadius + 1).
    [[nodiscard]] bool shouldLoad(int cx, int cz) const;

    /// Check if a chunk should be unloaded (outside unload radius = loadRadius + 2).
    /// Returns true ONLY if the chunk is outside the hysteresis band.
    [[nodiscard]] bool shouldUnload(int cx, int cz) const;

    /// Get chunks that need to be loaded, sorted by distance (closest first).
    /// @param maxCount Maximum number of chunks to return.
    /// @param alreadyLoaded Set of already loaded chunk keys (to skip).
    /// @return Vector of chunk coordinates to load.
    [[nodiscard]] std::vector<glm::ivec2> getChunksToLoad(int maxCount,
                                                          const std::unordered_set<int64_t>& alreadyLoaded) const;

    /// Get chunk keys that should be unloaded (outside unload radius).
    /// @param loadedChunks Currently loaded chunk keys.
    /// @return Vector of chunk keys to unload.
    [[nodiscard]] std::vector<int64_t> getChunksToUnload(const std::unordered_set<int64_t>& loadedChunks) const;

    /// Get the current simulation center.
    [[nodiscard]] glm::ivec2 simulationCenter() const { return m_playerChunk; }

    /// Get the current view center.
    [[nodiscard]] glm::ivec2 viewCenter() const { return m_playerChunk; }

    /// Get radii.
    [[nodiscard]] int simulationRadius() const { return m_simulationRadius; }
    [[nodiscard]] int viewRadius() const { return m_viewRadius; }
    [[nodiscard]] int loadRadius() const { return m_viewRadius + 1; }
    [[nodiscard]] int unloadRadius() const { return m_viewRadius + 3; } // loadRadius + 2

    /// Compute chunk key from chunk coordinates.
    static int64_t chunkKey(int cx, int cz);

private:
    /// Check if a chunk is within a circular radius of a center.
    [[nodiscard]] static bool isWithinRadius(int cx, int cz, int centerX, int centerZ, int radius);

    /// Compute squared distance from chunk to center.
    [[nodiscard]] static int distanceSq(int cx, int cz, int centerX, int centerZ);

    glm::ivec2 m_playerChunk = glm::ivec2(0);
    int m_simulationRadius = 8;
    int m_viewRadius = 16;
};

#endif // MECRAFT_CHUNK_TICKET_MANAGER_H
