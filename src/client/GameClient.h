#ifndef MECRAFT_GAME_CLIENT_H
#define MECRAFT_GAME_CLIENT_H

#include "../net/Transport.h"
#include "../net/Protocol.h"
#include "ClientWorld.h"
#include "ClientEntityStore.h"
#include <entt/entt.hpp>
#include <memory>

class ResourceMgr;

namespace client {

/// Client-side game logic. Manages the connection to the server,
/// sends input, and receives world state updates.
class GameClient {
public:
    GameClient();
    ~GameClient();

    /// Connect to a server via the given transport endpoint.
    void connect(std::unique_ptr<net::ITransportEndpoint> transport);

    /// Initialize the entity store with the ECS registry.
    void initEntityStore(entt::registry& registry, ResourceMgr* resourceMgr);

    /// Send a hello message to the server.
    void sendHello();

    /// Send the client's view configuration (render distance) to the server.
    void sendViewConfig(int renderDistance);

    /// Sample input and send to server. Called at the client's fixed update rate.
    void sendInput(float dt, const glm::vec3& moveInput,
                   const glm::vec2& lookDelta, bool jump, bool sneak, bool sprint);

    /// Process all pending messages from the server.
    void receiveMessages();

    /// Access the client-side world.
    [[nodiscard]] ClientWorld& clientWorld() { return m_clientWorld; }
    [[nodiscard]] const ClientWorld& clientWorld() const { return m_clientWorld; }

    /// Access the client-side entity store.
    [[nodiscard]] ClientEntityStore& entityStore() { return m_entityStore; }
    [[nodiscard]] const ClientEntityStore& entityStore() const { return m_entityStore; }

    /// Check if the client has received initial spawn chunks from the server.
    [[nodiscard]] bool areSpawnChunksReady() const { return m_spawnChunksReady; }

    /// Get the latest authoritative position from the server.
    [[nodiscard]] glm::vec3 getAuthoritativePosition() const { return m_authPosition; }

    /// Get the latest server snapshot.
    [[nodiscard]] const net::ServerSnapshot& lastSnapshot() const { return m_lastSnapshot; }

    /// Get the assigned client ID from the server.
    [[nodiscard]] net::ClientId getClientId() const { return m_clientId; }

private:
    void handleChunkData(const net::ChunkDataMessage& data);
    void handleServerSnapshot(const net::ServerSnapshot& snapshot);

    std::unique_ptr<net::ITransportEndpoint> m_transport;
    ClientWorld m_clientWorld;
    ClientEntityStore m_entityStore;
    net::ServerSnapshot m_lastSnapshot;
    glm::vec3 m_authPosition = glm::vec3(0.0f);
    net::ClientId m_clientId = 0;
    uint32_t m_inputSequence = 0;
    bool m_spawnChunksReady = false;
    int m_chunksReceived = 0;
    static constexpr int kSpawnChunksThreshold = 25;  // 5x5 area
};

} // namespace client

#endif // MECRAFT_GAME_CLIENT_H
