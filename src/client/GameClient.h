#ifndef MECRAFT_GAME_CLIENT_H
#define MECRAFT_GAME_CLIENT_H

#include "../net/Transport.h"
#include "../net/Protocol.h"
#include "ClientWorld.h"
#include "ClientEntityStore.h"
#include <entt/entt.hpp>
#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

class ResourceMgr;
namespace ecs {
class GameplayRegistry;
}

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
    void initEntityStore(ecs::GameplayRegistry& registry, ResourceMgr* resourceMgr);

    /// Send a hello message to the server.
    void sendHello();

    /// Send the client's view configuration (render distance) to the server.
    void sendViewConfig(int renderDistance);

    /// Sample input and send to server. Called at the client's fixed update rate.
    void sendInput(float dt, const glm::vec3& moveInput, const glm::vec2& lookDelta, bool jump, bool sneak,
                   bool sprint);
    void sendInput(float dt, const glm::vec3& moveInput, const glm::vec2& lookDelta, bool jump, bool sneak, bool sprint,
                   const glm::vec3& playerPosition, const glm::vec3& playerVelocity, float yaw, float pitch,
                   uint32_t actions = 0, uint8_t selectedHotbarSlot = 0);

    /// Send an authoritative block action request to the server.
    void sendBlockAction(const net::ClientBlockAction& action);
    void sendContainerOpenRequest(const glm::ivec3& blockPosition, const glm::vec3& playerPosition);
    void sendContainerSlotAction(const net::ClientContainerSlotAction& action);
    void sendContainerClose(uint32_t containerId);
    void sendChatMessage(const std::string& message);
    void sendCommandRequest(const std::string& command);
    void sendRespawnRequest();

    /// Process all pending messages from the server.
    void receiveMessages();

    using SystemMessageCallback = std::function<void(const net::ServerSystemMessage&)>;
    using ChatMessageCallback = std::function<void(const net::ServerChatMessage&)>;
    using CommandResultCallback = std::function<void(const net::CommandResultMessage&)>;
    using LocalModeCallback = std::function<void(net::NetworkGameplayMode)>;

    void setSystemMessageCallback(SystemMessageCallback callback) { m_systemMessageCallback = std::move(callback); }
    void setChatMessageCallback(ChatMessageCallback callback) { m_chatMessageCallback = std::move(callback); }
    void setCommandResultCallback(CommandResultCallback callback) { m_commandResultCallback = std::move(callback); }
    void setLocalModeCallback(LocalModeCallback callback) { m_localModeCallback = std::move(callback); }

    /// Access the client-side world.
    [[nodiscard]] ClientWorld& clientWorld() { return m_clientWorld; }
    [[nodiscard]] const ClientWorld& clientWorld() const { return m_clientWorld; }

    /// Access the client-side entity store.
    [[nodiscard]] ClientEntityStore& entityStore() { return m_entityStore; }
    [[nodiscard]] const ClientEntityStore& entityStore() const { return m_entityStore; }

    /// Check if the client has received initial spawn chunks from the server.
    [[nodiscard]] bool areSpawnChunksReady() const { return m_spawnChunksReady; }
    [[nodiscard]] int spawnChunksLoadedCount() const { return std::min(m_chunksReceived, kSpawnChunksThreshold); }
    [[nodiscard]] int spawnChunksTargetCount() const { return kSpawnChunksThreshold; }
    [[nodiscard]] bool hasServerHello() const { return m_hasServerHello; }

    /// Get the latest authoritative position from the server.
    [[nodiscard]] glm::vec3 getAuthoritativePosition() const { return m_authPosition; }

    /// Get the latest server snapshot.
    [[nodiscard]] const net::ServerSnapshot& lastSnapshot() const { return m_lastSnapshot; }
    [[nodiscard]] bool isPlayerDead() const { return m_lastSnapshot.playerDead || m_lastSnapshot.playerHealth == 0; }

    /// Get the assigned client ID from the server.
    [[nodiscard]] net::ClientId getClientId() const { return m_clientId; }
    [[nodiscard]] const net::ContainerSnapshotMessage* findContainerSnapshot(uint32_t containerId) const;
    [[nodiscard]] const net::ContainerSnapshotMessage* findContainerSnapshotAt(const glm::ivec3& blockPosition) const;
    bool consumeContainerClose(uint32_t containerId);

private:
    void handleChunkData(const net::ChunkDataMessage& data);
    void handleServerSnapshot(const net::ServerSnapshot& snapshot);
    void handleInventorySnapshot(const net::InventorySnapshotMessage& snapshot);
    void handleContainerSnapshot(const net::ContainerSnapshotMessage& snapshot);
    void handleContainerClose(const net::ContainerCloseMessage& close);
    void handleWorldStateSnapshot(const net::WorldStateSnapshotMessage& snapshot);
    void handlePlayerModeUpdate(const net::PlayerModeUpdateMessage& update);

    std::unique_ptr<net::ITransportEndpoint> m_transport;
    ClientWorld m_clientWorld;
    ClientEntityStore m_entityStore;
    entt::registry* m_ecsRegistry = nullptr;
    net::ServerSnapshot m_lastSnapshot;
    glm::vec3 m_authPosition = glm::vec3(0.0f);
    net::ClientId m_clientId = 0;
    uint32_t m_inputSequence = 0;
    uint32_t m_containerSequence = 0;
    uint32_t m_commandSequence = 0;
    uint32_t m_respawnSequence = 0;
    bool m_spawnChunksReady = false;
    bool m_hasServerHello = false;
    bool m_playerHurtLatched = false;
    int m_chunksReceived = 0;
    static constexpr int kSpawnChunksThreshold = 25; // 5x5 area
    SystemMessageCallback m_systemMessageCallback;
    ChatMessageCallback m_chatMessageCallback;
    CommandResultCallback m_commandResultCallback;
    LocalModeCallback m_localModeCallback;
    std::unordered_map<uint32_t, net::ContainerSnapshotMessage> m_containerSnapshots;
    std::unordered_set<uint32_t> m_closedContainerIds;
};

} // namespace client

#endif // MECRAFT_GAME_CLIENT_H
