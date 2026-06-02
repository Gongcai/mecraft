#ifndef MECRAFT_GAME_SERVER_H
#define MECRAFT_GAME_SERVER_H

#include "../net/Transport.h"
#include "../net/Protocol.h"
#include "../world/World.h"
#include <memory>
#include <vector>
#include <unordered_set>
#include <mutex>

class ThreadPool;

namespace server {

/// Tracks a connected client's state on the server side.
struct ConnectedClient {
    net::ClientId id = 0;
    std::unique_ptr<net::ITransportEndpoint> transport;
    glm::vec3 lastPosition = glm::vec3(0.0f);
    uint32_t lastAckedInput = 0;
    uint64_t lastSentChunkRevision = 0;
    std::unordered_set<int64_t> sentChunks;
};

/// Authoritative game server. Owns the World and runs the server tick loop.
/// In single-player mode, runs in-process alongside the GameClient.
class GameServer {
public:
    GameServer();
    ~GameServer();

    /// Initialize the server world with a seed.
    void init(uint32_t seed, ThreadPool* threadPool, int renderDistance);

    /// Accept a new client connection.
    void acceptClient(std::unique_ptr<net::ITransportEndpoint> transport, net::ClientId id);

    /// Run one server tick. Called from the game loop at the server tick rate.
    void tick(float dt);

    /// Access the authoritative world.
    [[nodiscard]] World& world() { return m_world; }
    [[nodiscard]] const World& world() const { return m_world; }

    /// Check if spawn chunks are ready.
    [[nodiscard]] bool areSpawnChunksReady() const { return m_spawnChunksReady; }

    /// Get the computed spawn position.
    [[nodiscard]] glm::vec3 getSpawnPosition() const { return m_spawnPosition; }

    /// Get the current server tick count.
    [[nodiscard]] net::TickId currentTick() const { return m_currentTick; }

private:
    void processClientMessages();
    void sendNewChunksToClients();
    void sendSnapshotsToClients();
    void sendChunkDataToClient(ConnectedClient& client, int cx, int cz);
    void sendBlockUpdatesToClients();
    void checkSpawnChunksReady();

    World m_world;
    std::vector<ConnectedClient> m_clients;
    std::vector<net::BlockUpdateEntry> m_pendingBlockUpdates;
    net::TickId m_currentTick = 0;
    bool m_spawnChunksReady = false;
    glm::vec3 m_spawnPosition = glm::vec3(0.0f);
};

} // namespace server

#endif // MECRAFT_GAME_SERVER_H
