#ifndef MECRAFT_GAME_SERVER_H
#define MECRAFT_GAME_SERVER_H

#include "../net/Transport.h"
#include "../net/Protocol.h"
#include "../world/World.h"
#include "../ecs/components/NetworkComponents.h"
#include "../save/SaveManager.h"
#include <entt/entt.hpp>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>

class ThreadPool;
namespace ecs {
class GameplayPipeline;
class GameplayRegistry;
}
namespace physics { class PhysicsSystem; }

namespace server {

/// Tracks a connected client's state on the server side.
struct ConnectedClient {
    net::ClientId id = 0;
    std::unique_ptr<net::ITransportEndpoint> transport;
    glm::vec3 lastPosition = glm::vec3(0.0f);
    glm::vec3 lastVelocity = glm::vec3(0.0f);
    float lastYaw = 0.0f;
    float lastPitch = 0.0f;
    uint8_t selectedHotbarSlot = 0;
    uint32_t lastAckedInput = 0;
    uint32_t pendingInputActions = 0;
    net::TickId helloTick = 0;
    int viewDistance = 8;  // Effective client streaming radius until ClientViewConfig arrives.
    bool receivedHello = false;
    bool receivedViewConfig = false;
    bool isAdmin = false;
    bool awaitingRespawn = false;
    bool deathDropsSpawned = false;
    int respawnSnapshotTicksRemaining = 0;
    bool hasLastInventorySnapshot = false;
    uint8_t lastInventorySnapshotSelected = 0;
    std::vector<net::InventorySlotData> lastInventorySnapshotSlots;
    net::NetworkGameplayMode gameplayMode = net::NetworkGameplayMode::Survival;
    net::EntityNetId playerNetId = 0;
    entt::entity ecsPlayerEntity = entt::null;
    std::unordered_set<net::EntityNetId> spawnedPlayerNetIds;
    std::unordered_set<net::EntityNetId> spawnedEntityNetIds;
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

    /// Initialize with save support. If savePath is non-empty, chunks will be
    /// persisted to disk and restored on subsequent sessions.
    void init(uint32_t seed, ThreadPool* threadPool, int renderDistance,
              std::filesystem::path savePath, std::string displayName = {});

    /// Explicit shutdown: flush pending saves before destruction.
    void shutdown();

    /// Accept a new client connection.
    void acceptClient(std::unique_ptr<net::ITransportEndpoint> transport, net::ClientId id);

    /// Set the ECS registry for entity synchronization.
    /// Must be called after ECS is initialized.
    void setEcsRegistry(entt::registry* registry);
    void setEcsRegistry(ecs::GameplayRegistry* registry);

    /// Run one server tick. Called from the game loop at the server tick rate.
    void tick(float dt);
    void tickInitialLoading(float dt, const glm::vec3& loadCenter);
    void setClientLoadCenter(const glm::vec3& loadCenter);

    /// Access the authoritative world.
    [[nodiscard]] World& world() { return m_world; }
    [[nodiscard]] const World& world() const { return m_world; }

    /// Access the save manager (null if saving is disabled).
    [[nodiscard]] save::SaveManager* saveManager() { return m_saveManager.get(); }

    /// Check if spawn chunks are ready.
    [[nodiscard]] bool areSpawnChunksReady() const { return m_spawnChunksReady; }
    [[nodiscard]] World::ChunkLoadProgress getWorldLoadProgress(const glm::vec3& loadCenter) const;

    /// Get the computed spawn position.
    [[nodiscard]] glm::vec3 getSpawnPosition() const { return m_spawnPosition; }

    /// Get the current server tick count.
    [[nodiscard]] net::TickId currentTick() const { return m_currentTick; }

    /// Persist currently tracked gameplay entities through SaveManager, if enabled.
    void savePersistentEntities();

    /// Restore persisted gameplay entities into the bound GameplayRegistry, if available.
    void restorePersistentEntities();

    /// Persist block entities such as chest inventories through SaveManager, if enabled.
    void saveBlockEntities();

    /// Restore block entities into the bound GameplayRegistry context, if available.
    void restoreBlockEntities();

private:
    void processClientMessages();
    void cleanupDisconnectedClients();
    [[nodiscard]] ConnectedClient* findClient(net::ClientId id);
    void broadcastPlayerDespawn(net::EntityNetId playerNetId, net::ClientId exceptClientId = 0);
    void handleClientBlockAction(ConnectedClient& client, const net::ClientBlockAction& action);
    void handleClientChatMessage(ConnectedClient& client, const net::ClientChatMessage& message);
    void handleClientCommandRequest(ConnectedClient& client, const net::ClientCommandRequest& request);
    void executeServerCommand(ConnectedClient& client, const net::ClientCommandRequest& request);
    void sendCommandResult(ConnectedClient& client, uint32_t sequence, bool success, const std::string& message);
    void sendSystemMessage(ConnectedClient& client, const std::string& message, net::ChatMessageKind kind);
    void broadcastSystemMessage(const std::string& message, net::ChatMessageKind kind);
    void broadcastWorldState();
    void broadcastPlayerMode(net::ClientId clientId, net::NetworkGameplayMode mode);
    void tickWorldSystems();
    void sendNewChunksToClients();
    void sendSnapshotsToClients();
    void sendInventorySnapshotsToClients();
    void sendChunkDataToClient(ConnectedClient& client, int cx, int cz);
    void sendBlockUpdatesToClients();
    void syncEntitiesToClients();
    void ensureOwnedEcsRuntime();
    [[nodiscard]] bool usingOwnedEcsRegistry() const;
    void tickServerEcs(float dt);
    void syncOwnedPlayerProxies();
    void updatePlayerLifecycle(float dt);
    void respawnPlayer(ConnectedClient& client);
    void dropPlayerInventory(ConnectedClient& client);
    [[nodiscard]] entt::entity resolvePlayerEntity(const ConnectedClient& client) const;
    [[nodiscard]] bool buildInventorySnapshot(const ConnectedClient& client,
                                              net::InventorySnapshotMessage& out) const;
    void destroyOwnedPlayerProxy(ConnectedClient& client);
    [[nodiscard]] net::EntitySpawnMessage makeEntitySpawnMessage(ecs::EntityNetId netId, entt::entity entity) const;
    [[nodiscard]] bool spawnMobEntity(const std::string& entityId, const glm::vec3& position);
    [[nodiscard]] bool spawnZombieEntity(const glm::vec3& position);
    [[nodiscard]] std::vector<save::PersistentEntityData> snapshotPersistentEntities() const;
    [[nodiscard]] std::vector<save::BlockEntityData> snapshotBlockEntities() const;
    void checkSpawnChunksReady();
    [[nodiscard]] net::BlockUpdateEntry makeBlockUpdateEntry(int x, int y, int z, BlockID blockId, int lightPatchRadius) const;
    [[nodiscard]] net::BlockUpdateEntry makeBlockOnlyUpdateEntry(int x, int y, int z, BlockID blockId) const;
    [[nodiscard]] net::BlockUpdateEntry makeSubChunkLightUpdateEntry(int64_t chunkKey, int scy) const;
    void syncPlayersToClients();

    World m_world;
    std::unique_ptr<save::SaveManager> m_saveManager;
    std::vector<ConnectedClient> m_clients;
    std::vector<net::BlockUpdateEntry> m_pendingBlockUpdates;

    // Entity sync state
    ecs::EntityNetId m_nextNetId = 1;
    std::unordered_map<ecs::EntityNetId, entt::entity> m_syncedEntities;
    entt::registry* m_ecsRegistry = nullptr;  // Non-owning pointer to ECS registry
    ecs::GameplayRegistry* m_gameplayRegistry = nullptr;  // Non-owning pointer when model factories are available
    std::unique_ptr<ecs::GameplayRegistry> m_ownedGameplayRegistry;
    std::unique_ptr<ecs::GameplayPipeline> m_ownedGameplayPipeline;
    std::unique_ptr<physics::PhysicsSystem> m_ownedPhysicsSystem;
    bool m_entitiesRestorePending = false;
    bool m_blockEntitiesRestorePending = false;

    net::TickId m_currentTick = 0;
    bool m_spawnChunksReady = false;
    bool m_shutdownDone = false;
    glm::vec3 m_spawnPosition = glm::vec3(0.0f);

    // Loaded metadata for deferred restore
    save::LevelMeta m_loadedMeta;
    bool m_hasLoadedMeta = false;

    // Autosave timer
    float m_autosaveTimer = 0.0f;
    static constexpr float AUTOSAVE_INTERVAL_SECONDS = 300.0f; // 5 minutes

    // Helper to save current level metadata
    void saveLevelMeta();

    // Weather type string conversion helpers
    static WeatherType weatherTypeFromString(const std::string& str) {
        if (str == "rain") return WeatherType::Rain;
        if (str == "storm") return WeatherType::Storm;
        if (str == "snow") return WeatherType::Snow;
        return WeatherType::Clear;
    }
    static const char* weatherTypeToString(WeatherType type) {
        switch (type) {
            case WeatherType::Rain: return "rain";
            case WeatherType::Storm: return "storm";
            case WeatherType::Snow: return "snow";
            default: return "clear";
        }
    }
};

} // namespace server

#endif // MECRAFT_GAME_SERVER_H
