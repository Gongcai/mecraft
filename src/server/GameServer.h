#ifndef MECRAFT_GAME_SERVER_H
#define MECRAFT_GAME_SERVER_H

#include "../net/Transport.h"
#include "../net/Protocol.h"
#include "../world/World.h"
#include "../ecs/components/NetworkComponents.h"
#include <entt/entt.hpp>
#include <memory>
#include <vector>
#include <unordered_set>
#include <unordered_map>

class ThreadPool;

namespace server {

/// Tracks a connected client's state on the server side.
struct ConnectedClient {
    net::ClientId id = 0;
    std::unique_ptr<net::ITransportEndpoint> transport;
    glm::vec3 lastPosition = glm::vec3(0.0f);
    glm::vec3 lastVelocity = glm::vec3(0.0f);
    float lastYaw = 0.0f;
    float lastPitch = 0.0f;
    uint32_t lastAckedInput = 0;
    net::TickId helloTick = 0;
    int viewDistance = 16;  // Client's render distance
    bool receivedHello = false;
    bool receivedViewConfig = false;
    net::EntityNetId playerNetId = 0;
    std::unordered_set<net::EntityNetId> spawnedPlayerNetIds;
    std::unordered_set<int64_t> sentChunks;  // Chunks this client has received
    int chunkSendLogCount = 0;
    int totalChunksSent = 0;
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

    /// Set the ECS registry for entity synchronization.
    /// Must be called after ECS is initialized.
    void setEcsRegistry(entt::registry* registry) { m_ecsRegistry = registry; }

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
    struct PendingRelightUpdate {
        int x = 0;
        int y = 0;
        int z = 0;
        BlockID blockId = 0;
        int64_t chunkKey = 0;
        uint64_t lightRevision = 0;
        int ticksWaited = 0;
    };

    void processClientMessages();
    void handleClientBlockAction(ConnectedClient& client, const net::ClientBlockAction& action);
    void sendNewChunksToClients();
    void sendSnapshotsToClients();
    void sendChunkDataToClient(ConnectedClient& client, int cx, int cz);
    void sendBlockUpdatesToClients();
    void sendRelightUpdatesToClients();
    void syncEntitiesToClients();
    void checkSpawnChunksReady();
    [[nodiscard]] net::BlockUpdateEntry makeBlockUpdateEntry(int x, int y, int z, BlockID blockId, int lightPatchRadius) const;
    [[nodiscard]] net::BlockUpdateEntry makeBlockOnlyUpdateEntry(int x, int y, int z, BlockID blockId) const;
    [[nodiscard]] net::BlockUpdateEntry makeChunkLightUpdateEntry(int64_t chunkKey) const;
    void syncPlayersToClients();

    World m_world;
    std::vector<ConnectedClient> m_clients;
    std::vector<net::BlockUpdateEntry> m_pendingBlockUpdates;
    std::vector<PendingRelightUpdate> m_pendingRelightUpdates;

    // Entity sync state
    ecs::EntityNetId m_nextNetId = 1;
    std::unordered_map<ecs::EntityNetId, entt::entity> m_syncedEntities;
    entt::registry* m_ecsRegistry = nullptr;  // Non-owning pointer to ECS registry

    net::TickId m_currentTick = 0;
    bool m_spawnChunksReady = false;
    glm::vec3 m_spawnPosition = glm::vec3(0.0f);
};

} // namespace server

#endif // MECRAFT_GAME_SERVER_H
