#include "GameServer.h"
#include "../world/World.h"
#include "../world/WeatherSystem.h"
#include "../world/block/Block.h"
#include "../thread/ThreadPool.h"
#include "../save/SaveManager.h"
#include "../ecs/GameplayRegistry.h"
#include "../ecs/SystemContext.h"
#include "../ecs/entity/EntityFactory.h"
#include "../ecs/entity/MobModelFactory.h"
#include "../ecs/systems/combat/DamageSystem.h"
#include "../ecs/systems/combat/DeathSystem.h"
#include "../ecs/systems/combat/PlayerMeleeSystem.h"
#include "../ecs/systems/item/ItemSpawnSystem.h"
#include "../ecs/systems/item/ItemPickupSystem.h"
#include "../ecs/systems/item/ItemLifetimeSystem.h"
#include "../ecs/systems/item/ItemMergeSystem.h"
#include "../ecs/systems/item/ItemPhysicsSystem.h"
#include "../ecs/systems/mob/MobAISystem.h"
#include "../ecs/systems/player/CharacterPhysicsSystem.h"
#include "../ecs/systems/world/BlockSupportSystem.h"
#include "../ecs/components/Components.h"
#include "../ecs/components/NetworkComponents.h"
#include "../physics/PhysicsSystem.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <sstream>
#include <string>

namespace server {
namespace {
constexpr net::EntityNetId kPlayerNetIdBase = 0x80000000u;
constexpr uint16_t kLightOnlyBlockUpdate = 0xFFFFu;
constexpr int kPlayerRespawnSnapshotRepeatTicks = 5;

std::string playerName(const net::ClientId id) {
    return "Player" + std::to_string(id);
}

std::string trimCommand(std::string command) {
    while (!command.empty() && (command.front() == ' ' || command.front() == '\t')) {
        command.erase(command.begin());
    }
    while (!command.empty() && (command.back() == ' ' || command.back() == '\t' ||
                                command.back() == '\r' || command.back() == '\n')) {
        command.pop_back();
    }
    return command;
}

net::NetworkWeatherType toNetworkWeather(const WeatherType type) {
    switch (type) {
    case WeatherType::Rain: return net::NetworkWeatherType::Rain;
    case WeatherType::Storm: return net::NetworkWeatherType::Storm;
    case WeatherType::Snow: return net::NetworkWeatherType::Snow;
    case WeatherType::Clear:
    default:
        return net::NetworkWeatherType::Clear;
    }
}

bool parseWeatherType(const std::string& value, WeatherType& out) {
    if (value == "clear") {
        out = WeatherType::Clear;
        return true;
    }
    if (value == "rain") {
        out = WeatherType::Rain;
        return true;
    }
    if (value == "storm" || value == "thunder") {
        out = WeatherType::Storm;
        return true;
    }
    if (value == "snow") {
        out = WeatherType::Snow;
        return true;
    }
    return false;
}

bool parseGameplayMode(const std::string& value, net::NetworkGameplayMode& out) {
    if (value == "creative" || value == "1" || value == "c") {
        out = net::NetworkGameplayMode::Creative;
        return true;
    }
    if (value == "survival" || value == "0" || value == "s") {
        out = net::NetworkGameplayMode::Survival;
        return true;
    }
    return false;
}

const char* modeName(const net::NetworkGameplayMode mode) {
    return mode == net::NetworkGameplayMode::Creative ? "creative" : "survival";
}

const char* weatherName(const WeatherType type) {
    switch (type) {
    case WeatherType::Rain: return "rain";
    case WeatherType::Storm: return "storm";
    case WeatherType::Snow: return "snow";
    case WeatherType::Clear:
    default:
        return "clear";
    }
}

bool hasInputAction(const uint32_t actions, const uint32_t bit) {
    return (actions & bit) != 0u;
}

bool inventorySnapshotSlotsEqual(const std::vector<net::InventorySlotData>& a,
                                 const std::vector<net::InventorySlotData>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].itemId != b[i].itemId || a[i].stackCount != b[i].stackCount) {
            return false;
        }
    }
    return true;
}

void updateCameraStateFromClient(ecs::CameraStateComponent& camera,
                                 const float yaw,
                                 const float pitch) {
    constexpr float kDegreesToRadians = 0.017453292519943295f;
    camera.yaw = yaw;
    camera.pitch = pitch;

    const float yawRad = yaw * kDegreesToRadians;
    const float pitchRad = pitch * kDegreesToRadians;
    const glm::vec3 front(std::cos(yawRad) * std::cos(pitchRad),
                          std::sin(pitchRad),
                          std::sin(yawRad) * std::cos(pitchRad));
    camera.front = glm::length(front) > 0.001f ? glm::normalize(front) : glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 right = glm::cross(camera.front, glm::vec3(0.0f, 1.0f, 0.0f));
    camera.right = glm::length(right) > 0.001f ? glm::normalize(right) : glm::vec3(1.0f, 0.0f, 0.0f);
    camera.up = glm::normalize(glm::cross(camera.right, camera.front));
}
}

GameServer::GameServer() = default;

GameServer::~GameServer() {
    shutdown();
}

void GameServer::setEcsRegistry(entt::registry* registry) {
    for (auto& client : m_clients) {
        destroyOwnedPlayerProxy(client);
    }
    m_ecsRegistry = registry;
    m_gameplayRegistry = nullptr;
    m_syncedEntities.clear();
    for (auto& client : m_clients) {
        client.spawnedEntityNetIds.clear();
        client.hasLastInventorySnapshot = false;
        client.lastInventorySnapshotSlots.clear();
    }
}

void GameServer::setEcsRegistry(ecs::GameplayRegistry* registry) {
    if (registry != m_ownedGameplayRegistry.get()) {
        for (auto& client : m_clients) {
            destroyOwnedPlayerProxy(client);
        }
    }
    m_gameplayRegistry = registry;
    m_ecsRegistry = registry != nullptr ? &registry->registry() : nullptr;
    m_syncedEntities.clear();
    for (auto& client : m_clients) {
        client.spawnedEntityNetIds.clear();
        client.hasLastInventorySnapshot = false;
        client.lastInventorySnapshotSlots.clear();
    }
    if (registry != nullptr && m_entitiesRestorePending) {
        restorePersistentEntities();
    }
}

void GameServer::ensureOwnedEcsRuntime() {
    if (!m_ownedGameplayRegistry) {
        m_ownedGameplayRegistry = std::make_unique<ecs::GameplayRegistry>();
    }
    if (!m_ownedPhysicsSystem) {
        m_ownedPhysicsSystem = std::make_unique<physics::PhysicsSystem>(&m_world);
    }
    if (m_gameplayRegistry == nullptr) {
        m_gameplayRegistry = m_ownedGameplayRegistry.get();
        m_ecsRegistry = &m_ownedGameplayRegistry->registry();
    }
}

bool GameServer::usingOwnedEcsRegistry() const {
    return m_ownedGameplayRegistry && m_gameplayRegistry == m_ownedGameplayRegistry.get();
}

void GameServer::destroyOwnedPlayerProxy(ConnectedClient& client) {
    if (m_ownedGameplayRegistry && client.ecsPlayerEntity != entt::null) {
        auto& reg = m_ownedGameplayRegistry->registry();
        if (reg.valid(client.ecsPlayerEntity)) {
            reg.destroy(client.ecsPlayerEntity);
        }
    }
    client.ecsPlayerEntity = entt::null;
    client.awaitingRespawn = false;
    client.deathDropsSpawned = false;
    client.respawnSnapshotTicksRemaining = 0;
    client.hasLastInventorySnapshot = false;
    client.lastInventorySnapshotSlots.clear();
}

entt::entity GameServer::resolvePlayerEntity(const ConnectedClient& client) const {
    if (m_ecsRegistry == nullptr) {
        return entt::null;
    }
    if (client.ecsPlayerEntity != entt::null && m_ecsRegistry->valid(client.ecsPlayerEntity)) {
        return client.ecsPlayerEntity;
    }

    auto playerView = m_ecsRegistry->view<ecs::LocalPlayerTag, ecs::HealthComponent>();
    if (playerView.begin() != playerView.end()) {
        return *playerView.begin();
    }
    return entt::null;
}

bool GameServer::buildInventorySnapshot(const ConnectedClient& client,
                                        net::InventorySnapshotMessage& out) const {
    if (m_ecsRegistry == nullptr) {
        return false;
    }

    const entt::entity playerEntity = resolvePlayerEntity(client);
    if (playerEntity == entt::null || !m_ecsRegistry->valid(playerEntity)) {
        return false;
    }

    const auto* inventoryData = m_ecsRegistry->try_get<ecs::InventoryDataComponent>(playerEntity);
    if (inventoryData == nullptr) {
        return false;
    }

    int selectedSlot = inventoryData->inventory.getSelectedSlot();
    if (const auto* inventoryState = m_ecsRegistry->try_get<ecs::InventoryComponent>(playerEntity)) {
        selectedSlot = inventoryState->selectedHotbarSlot;
    }
    out.selectedHotbarSlot = static_cast<uint8_t>(std::clamp(selectedSlot, 0, Inventory::HOTBAR_SIZE - 1));
    out.slots.clear();
    out.slots.reserve(Inventory::INVENTORY_SIZE);

    for (int slot = 0; slot < Inventory::INVENTORY_SIZE; ++slot) {
        const ItemStack stack = inventoryData->inventory.getSlotStack(slot);
        net::InventorySlotData slotData;
        if (!stack.isEmpty()) {
            slotData.itemId = static_cast<uint16_t>(std::clamp<uint32_t>(stack.itemId, 0u, 0xFFFFu));
            slotData.stackCount = static_cast<uint8_t>(std::clamp<uint32_t>(stack.count, 0u, 0xFFu));
        }
        out.slots.push_back(slotData);
    }

    return true;
}

void GameServer::dropPlayerInventory(ConnectedClient& client) {
    if (client.deathDropsSpawned || m_gameplayRegistry == nullptr || m_ecsRegistry == nullptr) {
        return;
    }

    const entt::entity playerEntity = resolvePlayerEntity(client);
    if (playerEntity == entt::null || !m_ecsRegistry->valid(playerEntity)) {
        return;
    }

    auto& reg = *m_ecsRegistry;
    auto* inventoryData = reg.try_get<ecs::InventoryDataComponent>(playerEntity);
    if (inventoryData == nullptr) {
        return;
    }

    glm::vec3 position = client.lastPosition;
    if (const auto* transform = reg.try_get<ecs::TransformComponent>(playerEntity)) {
        position = transform->position;
    }

    const glm::ivec3 dropBlockPos(static_cast<int>(std::floor(position.x)),
                                  static_cast<int>(std::floor(position.y)),
                                  static_cast<int>(std::floor(position.z)));

    bool droppedAny = false;
    for (int slot = 0; slot < Inventory::INVENTORY_SIZE; ++slot) {
        const ItemStack stack = inventoryData->inventory.getSlotStack(slot);
        if (stack.isEmpty()) {
            continue;
        }
        ecs::ItemSpawnSystem::spawn(*m_gameplayRegistry, stack.itemId, dropBlockPos, stack.count);
        inventoryData->inventory.setSlotStack(slot, {});
        droppedAny = true;
    }

    client.deathDropsSpawned = true;
    if (droppedAny) {
        sendSystemMessage(client, "Your items dropped at your death location.", net::ChatMessageKind::Warning);
    }
}

void GameServer::respawnPlayer(ConnectedClient& client) {
    if (m_ecsRegistry == nullptr) {
        return;
    }

    const entt::entity playerEntity = resolvePlayerEntity(client);
    if (playerEntity == entt::null || !m_ecsRegistry->valid(playerEntity)) {
        return;
    }

    auto& reg = *m_ecsRegistry;
    client.awaitingRespawn = false;
    client.deathDropsSpawned = false;
    client.respawnSnapshotTicksRemaining = kPlayerRespawnSnapshotRepeatTicks;
    client.lastPosition = m_spawnPosition;
    client.lastVelocity = glm::vec3(0.0f);
    client.pendingInputActions = 0;

    if (auto* transform = reg.try_get<ecs::TransformComponent>(playerEntity)) {
        transform->position = m_spawnPosition;
        transform->eyeHeight = 1.62f;
    } else {
        reg.emplace<ecs::TransformComponent>(playerEntity, m_spawnPosition, 1.62f);
    }

    if (auto* velocity = reg.try_get<ecs::VelocityComponent>(playerEntity)) {
        velocity->velocity = glm::vec3(0.0f);
    } else {
        reg.emplace<ecs::VelocityComponent>(playerEntity);
    }

    if (auto* physicsBody = reg.try_get<ecs::PhysicsBodyComponent>(playerEntity)) {
        physicsBody->body.position = m_spawnPosition;
        physicsBody->body.velocity = glm::vec3(0.0f);
    }

    auto* health = reg.try_get<ecs::HealthComponent>(playerEntity);
    if (health == nullptr) {
        health = &reg.emplace<ecs::HealthComponent>(playerEntity);
    }
    health->max = std::max(1, health->max);
    health->current = health->max;

    if (auto* hurt = reg.try_get<ecs::HurtEffectComponent>(playerEntity)) {
        hurt->classicHurtEffectPending = false;
    }

    if (auto* blockIntent = reg.try_get<ecs::BlockActionIntentComponent>(playerEntity)) {
        blockIntent->wantsBreak = false;
        blockIntent->wantsPlace = false;
    }

    sendSystemMessage(client, "Respawned at world spawn.", net::ChatMessageKind::Success);
}

void GameServer::updatePlayerLifecycle(const float dt) {
    static_cast<void>(dt);
    if (m_ecsRegistry == nullptr) {
        return;
    }

    auto& reg = *m_ecsRegistry;
    for (auto& client : m_clients) {
        const bool active = client.receivedHello &&
                            client.transport &&
                            client.transport->hasActiveRemote();
        if (!active) {
            continue;
        }

        const entt::entity playerEntity = resolvePlayerEntity(client);
        if (playerEntity == entt::null || !reg.valid(playerEntity)) {
            continue;
        }

        auto* health = reg.try_get<ecs::HealthComponent>(playerEntity);
        if (health == nullptr) {
            continue;
        }

        if (client.awaitingRespawn) {
            client.pendingInputActions = 0;
            client.lastVelocity = glm::vec3(0.0f);
            health->current = 0;
            if (auto* velocity = reg.try_get<ecs::VelocityComponent>(playerEntity)) {
                velocity->velocity = glm::vec3(0.0f);
            }
            if (auto* physicsBody = reg.try_get<ecs::PhysicsBodyComponent>(playerEntity)) {
                physicsBody->body.velocity = glm::vec3(0.0f);
            }
            continue;
        }

        if (health->current <= 0) {
            client.awaitingRespawn = true;
            health->current = 0;
            client.pendingInputActions = 0;
            client.lastVelocity = glm::vec3(0.0f);
            if (auto* velocity = reg.try_get<ecs::VelocityComponent>(playerEntity)) {
                velocity->velocity = glm::vec3(0.0f);
            }
            if (auto* physicsBody = reg.try_get<ecs::PhysicsBodyComponent>(playerEntity)) {
                physicsBody->body.velocity = glm::vec3(0.0f);
            }
            dropPlayerInventory(client);
            sendSystemMessage(client, "You died. Press R to respawn.", net::ChatMessageKind::Warning);
        }
    }
}

void GameServer::syncOwnedPlayerProxies() {
    if (!usingOwnedEcsRegistry() || m_ownedGameplayRegistry == nullptr) {
        return;
    }

    auto& reg = m_ownedGameplayRegistry->registry();
    for (auto& client : m_clients) {
        const bool active = client.receivedHello &&
                            client.transport &&
                            client.transport->hasActiveRemote();
        if (!active) {
            destroyOwnedPlayerProxy(client);
            continue;
        }

        if (client.ecsPlayerEntity == entt::null || !reg.valid(client.ecsPlayerEntity)) {
            client.ecsPlayerEntity =
                ecs::EntityFactory::createServerPlayerProxy(*m_gameplayRegistry,
                                                            client.lastPosition,
                                                            client.lastVelocity);
        } else {
            ecs::EntityFactory::ensureServerPlayerProxy(*m_gameplayRegistry,
                                                        client.ecsPlayerEntity,
                                                        client.lastPosition,
                                                        client.lastVelocity);
        }

        auto& camera = reg.get<ecs::CameraStateComponent>(client.ecsPlayerEntity);
        updateCameraStateFromClient(camera, client.lastYaw, client.lastPitch);

        auto& blockIntent = reg.get<ecs::BlockActionIntentComponent>(client.ecsPlayerEntity);
        blockIntent.wantsBreak = hasInputAction(client.pendingInputActions, net::ClientInputActions::Attack);
        blockIntent.wantsPlace = hasInputAction(client.pendingInputActions, net::ClientInputActions::UseItem);
    }
}

void GameServer::tickServerEcs(const float dt) {
    if (!usingOwnedEcsRegistry() || m_gameplayRegistry == nullptr) {
        return;
    }

    if (m_entitiesRestorePending) {
        restorePersistentEntities();
    }

    syncOwnedPlayerProxies();

    ecs::GameplayServices services;
    services.world = &m_world;
    services.worldView = &m_world;
    services.physicsSystem = m_ownedPhysicsSystem.get();

    ecs::SystemContext ctx{*m_gameplayRegistry, services, dt, m_currentTick};

    ecs::MobAISystem mobAI;
    mobAI.update(ctx);
    ecs::CharacterPhysicsSystem characterPhysics;
    characterPhysics.update(ctx);
    ecs::PlayerMeleeSystem playerMelee;
    playerMelee.update(ctx);
    ecs::DamageSystem damage;
    damage.update(ctx);
    updatePlayerLifecycle(dt);
    ecs::DeathSystem death;
    death.update(ctx);
    ecs::ItemPhysicsSystem itemPhysics;
    itemPhysics.update(ctx);
    ecs::ItemMergeSystem itemMerge;
    itemMerge.update(ctx);
    ecs::ItemPickupSystem itemPickup;
    itemPickup.update(ctx);
    ecs::ItemLifetimeSystem itemLifetime;
    itemLifetime.update(ctx);

    for (auto& client : m_clients) {
        client.pendingInputActions = 0;
    }
}

void GameServer::init(uint32_t seed, ThreadPool* threadPool, int renderDistance) {
    m_world.setRenderDistance(renderDistance);
    m_world.setThreadPool(threadPool);
    m_world.init(seed);

    // Register block change callback to collect dirty blocks for BlockUpdateBatch
    m_world.setBlockChangeCallback([this](int x, int y, int z, BlockID newBlockId) {
        m_pendingBlockUpdates.push_back(makeBlockOnlyUpdateEntry(x, y, z, newBlockId));
    });

    m_world.setLightChangeCallback([this](int64_t chunkKey, uint32_t dirtySubChunkMask) {
        for (int scy = 0; scy < Chunk::NUM_SUB_CHUNKS; ++scy) {
            if ((dirtySubChunkMask & (1u << scy)) == 0u) {
                continue;
            }
            net::BlockUpdateEntry entry = makeSubChunkLightUpdateEntry(chunkKey, scy);
            if (!entry.packedLightPatch.empty()) {
                m_pendingBlockUpdates.push_back(std::move(entry));
            }
        }
    });

    // Compute spawn position from world surface
    constexpr float kSpawnHeightOffset = 2.0f;
    const int surfaceY = m_world.getSurfaceY(0, 0);
    m_spawnPosition = glm::vec3(0.0f, static_cast<float>(surfaceY + kSpawnHeightOffset), 0.0f);
    std::printf("[Server] World initialized seed=%u renderDistance=%d spawn=(%.1f, %.1f, %.1f)\n",
                seed,
                renderDistance,
                m_spawnPosition.x,
                m_spawnPosition.y,
                m_spawnPosition.z);
    std::fflush(stdout);
    ensureOwnedEcsRuntime();
    m_entitiesRestorePending = m_saveManager != nullptr;
}

void GameServer::init(uint32_t seed, ThreadPool* threadPool, int renderDistance,
                      std::filesystem::path savePath, std::string displayName) {
    // Create save manager if path is provided
    if (!savePath.empty()) {
        if (displayName.empty()) {
            displayName = savePath.filename().string();
        }

        m_saveManager = std::make_unique<save::SaveManager>(std::move(savePath));
        m_saveManager->setThreadPool(threadPool);
        m_saveManager->paths().ensureDirectories();

        // Try to load existing level metadata
        save::LevelMeta meta;
        meta.seed = seed;
        if (m_saveManager->loadLevelMeta(meta)) {
            seed = meta.seed;
            // Restore time and weather after world init
            m_loadedMeta = meta;
            m_hasLoadedMeta = true;
            std::printf("[Server] Loaded existing world (seed=%u)\n", seed);
        } else {
            // New world - set creation timestamp
            meta.seed = seed;
            meta.createdUtc = save::SaveManager::currentUtcTimestamp();
            meta.lastSavedUtc = meta.createdUtc;
            meta.displayName = displayName.empty() ? "New World" : displayName;
            m_saveManager->saveLevelMeta(meta);
            m_loadedMeta = meta;
            std::printf("[Server] Created new world (seed=%u)\n", seed);
        }

        m_world.setSaveManager(m_saveManager.get());
    }

    // Delegate to the base init for world setup
    init(seed, threadPool, renderDistance);

    // Restore time and weather from loaded metadata
    if (m_hasLoadedMeta) {
        m_world.getDayNightSystem().setTimeOfDay(m_loadedMeta.timeOfDay);
        m_world.getWeatherSystem().setDebugWeatherPresetInstant(
            weatherTypeFromString(m_loadedMeta.weatherType));
        m_spawnPosition = glm::vec3(m_loadedMeta.spawnX, m_loadedMeta.spawnY, m_loadedMeta.spawnZ);
    }
}

void GameServer::shutdown() {
    if (m_shutdownDone) return;
    m_shutdownDone = true;

    // Flush all pending chunk saves
    m_world.flushSaves();

    // Save level metadata with current state
    if (m_saveManager) {
        savePersistentEntities();
        saveLevelMeta();
    }
}

void GameServer::saveLevelMeta() {
    if (!m_saveManager) return;

    save::LevelMeta meta;
    meta.seed = m_world.getSeed();
    meta.spawnX = m_spawnPosition.x;
    meta.spawnY = m_spawnPosition.y;
    meta.spawnZ = m_spawnPosition.z;
    meta.timeOfDay = m_world.getDayNightSystem().getTimeOfDay();
    meta.totalGameTime = m_world.getDayNightSystem().getTotalGameTime();
    meta.elapsedDays = m_world.getDayNightSystem().getElapsedDays();

    const auto& weather = m_world.getWeatherSystem().getTargetState();
    meta.weatherType = weatherTypeToString(weather.type);
    meta.weatherWetness = weather.wetness;
    meta.weatherStorm = weather.storm;
    meta.weatherAerialReduction = weather.aerialReduction;

    // Preserve timestamps from loaded meta, update lastSavedUtc
    meta.createdUtc = m_loadedMeta.createdUtc;
    meta.lastSavedUtc = save::SaveManager::currentUtcTimestamp();
    meta.screenshotPath = "thumb.png";
    meta.displayName = m_loadedMeta.displayName;

    m_saveManager->saveLevelMeta(meta);
    m_loadedMeta = meta;
}

void GameServer::acceptClient(std::unique_ptr<net::ITransportEndpoint> transport, net::ClientId id) {
    ConnectedClient client;
    client.id = id;
    client.transport = std::move(transport);
    client.lastPosition = m_spawnPosition;
    client.playerNetId = kPlayerNetIdBase | id;
    client.isAdmin = id == 1;
    m_clients.push_back(std::move(client));
    std::printf("[Server] Accepted transport slot for client %u admin=%d\n", id, id == 1 ? 1 : 0);
    std::fflush(stdout);
}

void GameServer::tick(float dt) {
    // Process incoming client messages first so block edits and player poses
    // participate in this tick's world/light update before snapshots are sent.
    processClientMessages();
    cleanupDisconnectedClients();
    tickWorldSystems();

    // Periodic autosave
    if (m_saveManager) {
        m_autosaveTimer += dt;
        if (m_autosaveTimer >= AUTOSAVE_INTERVAL_SECONDS) {
            m_autosaveTimer = 0.0f;
            m_world.flushSaves();
            saveLevelMeta();
        }
    }

    glm::vec3 loadCenter = m_spawnPosition;
    for (const auto& client : m_clients) {
        if (client.receivedHello) {
            loadCenter = client.lastPosition;
            break;
        }
    }
    m_world.update(loadCenter, dt);
    tickServerEcs(dt);
    if (!usingOwnedEcsRegistry()) {
        updatePlayerLifecycle(dt);
    }

    // Send new chunks to clients
    sendNewChunksToClients();

    // Send authoritative snapshots to clients
    sendSnapshotsToClients();
    sendInventorySnapshotsToClients();

    // Sync entities (spawn/despawn/snapshot)
    syncEntitiesToClients();
    syncPlayersToClients();

    // Send pending block updates to clients
    sendBlockUpdatesToClients();

    // Check if spawn chunks are ready
    if (!m_spawnChunksReady) {
        checkSpawnChunksReady();
    }

    ++m_currentTick;
}

void GameServer::tickInitialLoading(const float dt, const glm::vec3& loadCenter) {
    processClientMessages();
    cleanupDisconnectedClients();
    tickWorldSystems();

    m_world.updateForInitialLoad(loadCenter, dt);
    sendNewChunksToClients();
    sendSnapshotsToClients();
    sendInventorySnapshotsToClients();
    sendBlockUpdatesToClients();

    if (!m_spawnChunksReady) {
        checkSpawnChunksReady();
    }

    ++m_currentTick;
}

void GameServer::setClientLoadCenter(const glm::vec3& loadCenter) {
    for (auto& client : m_clients) {
        client.lastPosition = loadCenter;
    }
}

World::ChunkLoadProgress GameServer::getWorldLoadProgress(const glm::vec3& loadCenter) const {
    return m_world.getChunkLoadProgress(loadCenter);
}

void GameServer::tickWorldSystems() {
    m_world.fluidSystem().processScheduledBlockTicks(m_currentTick, 4096);
    ecs::BlockSupportSystem::processWorldQueue(m_world, 1024);
}

void GameServer::processClientMessages() {
    for (auto& client : m_clients) {
        net::Packet packet;
        while (client.transport->tryReceive(packet)) {
            switch (packet.type) {
            case net::MessageType::ClientHello: {
                client.receivedHello = true;
                client.helloTick = m_currentTick;
                client.sentChunks.clear();
                client.chunkSendLogCount = 0;
                client.totalChunksSent = 0;
                if (packet.inProcessPayload.has_value()) {
                    const auto& hello = std::any_cast<const net::ClientHello&>(packet.inProcessPayload);
                    std::printf("[Server] ClientHello client=%u protocol=%u\n",
                                client.id,
                                hello.protocolVersion);
                } else {
                    std::printf("[Server] ClientHello client=%u without decoded payload\n", client.id);
                }
                std::fflush(stdout);

                // Respond with ServerHello
                net::Packet response;
                response.channel = net::PacketChannel::ReliableControl;
                response.type = net::MessageType::ServerHello;
                net::ServerHello hello;
                hello.protocolVersion = 1;
                hello.assignedId = client.id;
                hello.spawnPosition = m_spawnPosition;
                response.inProcessPayload = hello;
                client.transport->send(std::move(response));
                sendSystemMessage(client,
                                  "Connected as " + playerName(client.id) +
                                      (client.isAdmin ? " (admin)" : ""),
                                  net::ChatMessageKind::Success);
                broadcastWorldState();
                broadcastPlayerMode(client.id, client.gameplayMode);
                break;
            }
            case net::MessageType::ClientInput: {
                if (packet.inProcessPayload.has_value()) {
                    const auto& input = std::any_cast<const net::ClientInput&>(packet.inProcessPayload);
                    client.lastAckedInput = input.sequence;
                    client.lastYaw = input.yaw;
                    client.lastPitch = input.pitch;
                    if (!client.awaitingRespawn && client.respawnSnapshotTicksRemaining <= 0) {
                        client.lastPosition = input.playerPosition;
                        client.lastVelocity = input.playerVelocity;
                        client.pendingInputActions |= input.actions;
                    }
                }
                break;
            }
            case net::MessageType::ClientReady:
                // Client is ready to receive world data; nothing special needed.
                break;
            case net::MessageType::ClientViewConfig: {
                client.receivedViewConfig = true;
                if (packet.inProcessPayload.has_value()) {
                    const auto& config = std::any_cast<const net::ClientViewConfig&>(packet.inProcessPayload);
                    client.viewDistance = std::max(1, config.renderDistance);
                    std::printf("[Server] ClientViewConfig client=%u renderDistance=%d\n",
                                client.id,
                                client.viewDistance);
                    std::fflush(stdout);
                }
                break;
            }
            case net::MessageType::ClientBlockAction: {
                if (packet.inProcessPayload.has_value()) {
                    const auto& action = std::any_cast<const net::ClientBlockAction&>(packet.inProcessPayload);
                    if (!client.awaitingRespawn && client.respawnSnapshotTicksRemaining <= 0) {
                        handleClientBlockAction(client, action);
                    }
                }
                break;
            }
            case net::MessageType::ClientChatMessage: {
                if (packet.inProcessPayload.has_value()) {
                    const auto& message = std::any_cast<const net::ClientChatMessage&>(packet.inProcessPayload);
                    handleClientChatMessage(client, message);
                }
                break;
            }
            case net::MessageType::ClientCommandRequest: {
                if (packet.inProcessPayload.has_value()) {
                    const auto& request = std::any_cast<const net::ClientCommandRequest&>(packet.inProcessPayload);
                    handleClientCommandRequest(client, request);
                }
                break;
            }
            case net::MessageType::ClientRespawnRequest: {
                if (packet.inProcessPayload.has_value() && client.awaitingRespawn) {
                    respawnPlayer(client);
                }
                break;
            }
            default:
                break;
            }
        }
    }
}

void GameServer::broadcastPlayerDespawn(const net::EntityNetId playerNetId, const net::ClientId exceptClientId) {
    if (playerNetId == 0) {
        return;
    }

    for (auto& client : m_clients) {
        client.spawnedPlayerNetIds.erase(playerNetId);
    }

    for (auto& client : m_clients) {
        if (client.id == exceptClientId || !client.receivedHello || !client.transport || !client.transport->hasActiveRemote()) {
            continue;
        }

        net::Packet packet;
        packet.channel = net::PacketChannel::ReliableWorld;
        packet.type = net::MessageType::EntityDespawn;
        net::EntityDespawnMessage msg;
        msg.netId = playerNetId;
        packet.inProcessPayload = msg;
        client.transport->send(std::move(packet));
    }
}

void GameServer::cleanupDisconnectedClients() {
    for (auto& client : m_clients) {
        if (!client.transport || client.transport->isConnected()) {
            continue;
        }

        if (client.receivedHello) {
            std::printf("[Server] Removing disconnected client %u netId=%u\n",
                        client.id,
                        client.playerNetId);
            std::fflush(stdout);
            broadcastPlayerDespawn(client.playerNetId, client.id);
        }
        destroyOwnedPlayerProxy(client);
        client.receivedHello = false;
    }

    m_clients.erase(std::remove_if(m_clients.begin(), m_clients.end(), [](const ConnectedClient& client) {
        return !client.transport || !client.transport->isConnected();
    }), m_clients.end());

    for (auto& client : m_clients) {
        if (!client.receivedHello || !client.transport || client.transport->hasActiveRemote()) {
            continue;
        }

        std::printf("[Server] Client %u lost remote peer netId=%u\n",
                    client.id,
                    client.playerNetId);
        std::fflush(stdout);
        broadcastPlayerDespawn(client.playerNetId, client.id);
        destroyOwnedPlayerProxy(client);
        client.receivedHello = false;
        client.receivedViewConfig = false;
        client.lastAckedInput = 0;
        client.pendingInputActions = 0;
        client.awaitingRespawn = false;
        client.deathDropsSpawned = false;
        client.hasLastInventorySnapshot = false;
        client.lastInventorySnapshotSlots.clear();
        client.helloTick = 0;
        client.sentChunks.clear();
        client.spawnedPlayerNetIds.clear();
        client.spawnedEntityNetIds.clear();
        client.chunkSendLogCount = 0;
        client.totalChunksSent = 0;
        client.lastPosition = m_spawnPosition;
        client.lastVelocity = glm::vec3(0.0f);
        client.lastYaw = 0.0f;
        client.lastPitch = 0.0f;
    }
}

ConnectedClient* GameServer::findClient(const net::ClientId id) {
    for (auto& client : m_clients) {
        if (client.id == id) {
            return &client;
        }
    }
    return nullptr;
}

void GameServer::handleClientChatMessage(ConnectedClient& client, const net::ClientChatMessage& message) {
    std::string text = trimCommand(message.message);
    if (text.empty()) {
        return;
    }
    if (text.size() > 240) {
        text.resize(240);
    }

    net::Packet packet;
    packet.channel = net::PacketChannel::ReliableChat;
    packet.type = net::MessageType::ServerChatMessage;
    net::ServerChatMessage out;
    out.senderId = client.id;
    out.senderName = playerName(client.id);
    out.message = text;
    packet.inProcessPayload = std::move(out);

    for (auto& receiver : m_clients) {
        if (!receiver.receivedHello || !receiver.transport || !receiver.transport->hasActiveRemote()) {
            continue;
        }
        net::Packet copy = packet;
        receiver.transport->send(std::move(copy));
    }
}

void GameServer::handleClientCommandRequest(ConnectedClient& client, const net::ClientCommandRequest& request) {
    executeServerCommand(client, request);
}

void GameServer::executeServerCommand(ConnectedClient& client, const net::ClientCommandRequest& request) {
    std::string command = trimCommand(request.command);
    if (!command.empty() && command.front() == '/') {
        command.erase(command.begin());
    }
    std::istringstream iss(command);
    std::string primary;
    iss >> primary;

    if (primary.empty()) {
        sendCommandResult(client, request.sequence, false, "Empty command.");
        return;
    }

    if (primary == "help") {
        sendCommandResult(client,
                          request.sequence,
                          true,
                          "Commands: /help, /list, /gamemode <survival|creative> [clientId], /time set <0..1200>, /weather <clear|rain|storm|snow>, /summon zombie");
        return;
    }

    if (primary == "list") {
        std::string list = "Online players:";
        for (const auto& online : m_clients) {
            if (online.receivedHello && online.transport && online.transport->hasActiveRemote()) {
                list += " " + playerName(online.id) + "(" + std::to_string(online.id) + ")";
            }
        }
        sendCommandResult(client, request.sequence, true, list);
        return;
    }

    if (!client.isAdmin) {
        sendCommandResult(client, request.sequence, false, "You do not have permission to use this command.");
        return;
    }

    if (primary == "gamemode") {
        std::string modeStr;
        iss >> modeStr;
        net::NetworkGameplayMode mode = net::NetworkGameplayMode::Survival;
        if (!parseGameplayMode(modeStr, mode)) {
            sendCommandResult(client, request.sequence, false, "Usage: /gamemode <survival|creative> [clientId]");
            return;
        }

        net::ClientId targetId = client.id;
        std::string idStr;
        iss >> idStr;
        if (!idStr.empty()) {
            char* endPtr = nullptr;
            const unsigned long parsed = std::strtoul(idStr.c_str(), &endPtr, 10);
            if (endPtr == idStr.c_str() || *endPtr != '\0') {
                sendCommandResult(client, request.sequence, false, "Invalid client id.");
                return;
            }
            targetId = static_cast<net::ClientId>(parsed);
        }

        ConnectedClient* target = findClient(targetId);
        if (!target || !target->receivedHello) {
            sendCommandResult(client, request.sequence, false, "Target client is not online.");
            return;
        }

        target->gameplayMode = mode;
        broadcastPlayerMode(target->id, mode);
        const std::string message = "Set " + playerName(target->id) + " to " + modeName(mode) + " mode.";
        sendCommandResult(client, request.sequence, true, message);
        broadcastSystemMessage(message, net::ChatMessageKind::Success);
        return;
    }

    if (primary == "time") {
        std::string secondary;
        iss >> secondary;
        if (secondary != "set") {
            sendCommandResult(client, request.sequence, false, "Usage: /time set <0..1200>");
            return;
        }
        std::string valueStr;
        iss >> valueStr;
        char* endPtr = nullptr;
        const float value = std::strtof(valueStr.c_str(), &endPtr);
        if (valueStr.empty() || endPtr == valueStr.c_str() || *endPtr != '\0' || value < 0.0f || value > 1200.0f) {
            sendCommandResult(client, request.sequence, false, "Usage: /time set <0..1200>");
            return;
        }

        m_world.getDayNightSystem().setTimeOfDay(value);
        broadcastWorldState();
        const std::string message = "Time set to " + std::to_string(static_cast<int>(value)) + ".";
        sendCommandResult(client, request.sequence, true, message);
        broadcastSystemMessage(message, net::ChatMessageKind::Success);
        return;
    }

    if (primary == "weather") {
        std::string value;
        iss >> value;
        WeatherType weather = WeatherType::Clear;
        if (!parseWeatherType(value, weather)) {
            sendCommandResult(client, request.sequence, false, "Usage: /weather <clear|rain|storm|snow>");
            return;
        }

        m_world.getWeatherSystem().setDebugWeatherPresetInstant(weather);
        broadcastWorldState();
        const std::string message = std::string("Weather set to ") + weatherName(weather) + ".";
        sendCommandResult(client, request.sequence, true, message);
        broadcastSystemMessage(message, net::ChatMessageKind::Success);
        return;
    }

    if (primary == "summon") {
        std::string entityType;
        iss >> entityType;
        if (entityType != "zombie") {
            sendCommandResult(client, request.sequence, false, "Usage: /summon zombie");
            return;
        }

        constexpr float kSummonDistance = 2.0f;
        constexpr float kDegreesToRadians = 0.017453292519943295f;
        const float yawRad = client.lastYaw * kDegreesToRadians;
        const glm::vec3 forward(std::cos(yawRad), 0.0f, std::sin(yawRad));
        const glm::vec3 basePosition = client.receivedHello ? client.lastPosition : m_spawnPosition;
        const glm::vec3 spawnPosition = basePosition + forward * kSummonDistance;

        if (!spawnZombieEntity(spawnPosition)) {
            sendCommandResult(client, request.sequence, false, "No ECS registry is available for entity spawning.");
            return;
        }

        const std::string message = "Summoned zombie.";
        sendCommandResult(client, request.sequence, true, message);
        broadcastSystemMessage(message, net::ChatMessageKind::Success);
        return;
    }

    sendCommandResult(client, request.sequence, false, "Unknown command: " + primary);
}

void GameServer::sendCommandResult(ConnectedClient& client,
                                   const uint32_t sequence,
                                   const bool success,
                                   const std::string& message) {
    if (!client.transport || !client.transport->hasActiveRemote()) {
        return;
    }
    net::Packet packet;
    packet.channel = net::PacketChannel::ReliableChat;
    packet.type = net::MessageType::CommandResult;
    net::CommandResultMessage result;
    result.sequence = sequence;
    result.success = success;
    result.message = message;
    packet.inProcessPayload = std::move(result);
    client.transport->send(std::move(packet));
}

void GameServer::sendSystemMessage(ConnectedClient& client,
                                   const std::string& message,
                                   const net::ChatMessageKind kind) {
    if (!client.transport || !client.transport->hasActiveRemote()) {
        return;
    }
    net::Packet packet;
    packet.channel = net::PacketChannel::ReliableChat;
    packet.type = net::MessageType::ServerSystemMessage;
    net::ServerSystemMessage system;
    system.kind = kind;
    system.message = message;
    packet.inProcessPayload = std::move(system);
    client.transport->send(std::move(packet));
}

void GameServer::broadcastSystemMessage(const std::string& message, const net::ChatMessageKind kind) {
    for (auto& client : m_clients) {
        if (!client.receivedHello) {
            continue;
        }
        sendSystemMessage(client, message, kind);
    }
}

void GameServer::broadcastWorldState() {
    net::Packet packet;
    packet.channel = net::PacketChannel::ReliableWorld;
    packet.type = net::MessageType::WorldStateSnapshot;
    net::WorldStateSnapshotMessage snapshot;
    snapshot.timeOfDay = m_world.getDayNightSystem().getTimeOfDay();
    snapshot.weather = toNetworkWeather(m_world.getWeatherSystem().getTargetState().type);
    packet.inProcessPayload = snapshot;

    for (auto& client : m_clients) {
        if (!client.receivedHello || !client.transport || !client.transport->hasActiveRemote()) {
            continue;
        }
        net::Packet copy = packet;
        client.transport->send(std::move(copy));
    }
}

void GameServer::broadcastPlayerMode(const net::ClientId clientId, const net::NetworkGameplayMode mode) {
    net::Packet packet;
    packet.channel = net::PacketChannel::ReliableWorld;
    packet.type = net::MessageType::PlayerModeUpdate;
    net::PlayerModeUpdateMessage update;
    update.clientId = clientId;
    update.mode = mode;
    packet.inProcessPayload = update;

    for (auto& client : m_clients) {
        if (!client.receivedHello || !client.transport || !client.transport->hasActiveRemote()) {
            continue;
        }
        net::Packet copy = packet;
        client.transport->send(std::move(copy));
    }
}

void GameServer::handleClientBlockAction(ConnectedClient& client, const net::ClientBlockAction& action) {
    constexpr float kMaxActionDistance = 6.5f;
    const glm::ivec3 actionBlock = action.action == net::ClientBlockActionType::Place
        ? action.placeBlock
        : action.targetBlock;
    const glm::vec3 blockCenter = glm::vec3(actionBlock) + glm::vec3(0.5f);
    const glm::vec3 diff = action.playerPosition - blockCenter;
    const float distSq = diff.x * diff.x + diff.y * diff.y + diff.z * diff.z;
    if (distSq > kMaxActionDistance * kMaxActionDistance) {
        return;
    }

    client.lastPosition = action.playerPosition;

    if (action.action == net::ClientBlockActionType::Break) {
        const BlockID target = m_world.getBlock(action.targetBlock.x, action.targetBlock.y, action.targetBlock.z);
        if (target == BlockIds::AIR || !BlockRegistry::get(target).isSelectable) {
            return;
        }
        m_world.setBlock(action.targetBlock.x, action.targetBlock.y, action.targetBlock.z, BlockIds::AIR);
        std::printf("[Server] ClientBlockAction break client=%u block=(%d,%d,%d)\n",
                    client.id,
                    action.targetBlock.x,
                    action.targetBlock.y,
                    action.targetBlock.z);
        std::fflush(stdout);
        return;
    }

    if (action.blockState == BlockIds::AIR) {
        return;
    }
    if (m_world.getBlock(action.placeBlock.x, action.placeBlock.y, action.placeBlock.z) != BlockIds::AIR) {
        return;
    }

    m_world.setBlock(action.placeBlock.x,
                     action.placeBlock.y,
                     action.placeBlock.z,
                     static_cast<BlockID>(action.blockState));
    std::printf("[Server] ClientBlockAction place client=%u block=(%d,%d,%d) state=%u\n",
                client.id,
                action.placeBlock.x,
                action.placeBlock.y,
                action.placeBlock.z,
                static_cast<unsigned>(action.blockState));
    std::fflush(stdout);
}

void GameServer::sendNewChunksToClients() {
    for (auto& client : m_clients) {
        if (!client.receivedHello || !client.transport || !client.transport->hasActiveRemote() ||
            client.helloTick == m_currentTick) {
            continue;
        }

        const auto& activeChunks = m_world.getActiveChunks();

        // Build a temporary ticket manager for this client's view distance
        ChunkTicketManager clientTicketMgr;
        clientTicketMgr.setViewRadius(client.viewDistance);
        clientTicketMgr.setSimulationRadius(m_world.ticketManager().simulationRadius());
        const int playerChunkX = static_cast<int>(std::floor(client.lastPosition.x / 16.0f));
        const int playerChunkZ = static_cast<int>(std::floor(client.lastPosition.z / 16.0f));
        clientTicketMgr.updatePlayerPosition(playerChunkX, playerChunkZ);

        // Send initial spawn chunks aggressively so the client can enable
        // physics only after enough terrain is present.
        const int maxChunkSendsPerTick = client.totalChunksSent < 81 ? 96 : 24;
        int sent = 0;

        // Get prioritized chunks to send
        const auto chunksToSend = clientTicketMgr.getChunksToLoad(
            maxChunkSendsPerTick * 2, client.sentChunks);

        for (const auto& pos : chunksToSend) {
            if (sent >= maxChunkSendsPerTick) break;

            const int64_t key = ChunkTicketManager::chunkKey(pos.x, pos.y);
            auto it = activeChunks.find(key);
            if (it != activeChunks.end() && it->second) {
                sendChunkDataToClient(client, pos.x, pos.y);
                client.sentChunks.insert(key);
                ++sent;
            }
        }

        // Unload chunks outside the client's unload radius
        std::vector<int64_t> toUnload;
        for (const int64_t key : client.sentChunks) {
            const int cx = static_cast<int>(key >> 32);
            const int cz = static_cast<int>(static_cast<int32_t>(key & 0xFFFFFFFF));
            if (clientTicketMgr.shouldUnload(cx, cz)) {
                toUnload.push_back(key);
            }
        }

        for (const int64_t key : toUnload) {
            const int cx = static_cast<int>(key >> 32);
            const int cz = static_cast<int>(static_cast<int32_t>(key & 0xFFFFFFFF));

            net::Packet packet;
            packet.channel = net::PacketChannel::ReliableWorld;
            packet.type = net::MessageType::ChunkUnload;
            net::ChunkUnloadMessage unloadMsg;
            unloadMsg.chunkX = cx;
            unloadMsg.chunkZ = cz;
            packet.inProcessPayload = unloadMsg;
            client.transport->send(std::move(packet));

            client.sentChunks.erase(key);
        }
    }
}

void GameServer::sendSnapshotsToClients() {
    std::vector<entt::entity> hurtEventsSent;

    for (auto& client : m_clients) {
        if (!client.receivedHello || !client.transport || !client.transport->hasActiveRemote()) {
            continue;
        }
        net::Packet packet;
        packet.channel = net::PacketChannel::UnreliableState;
        packet.type = net::MessageType::ServerSnapshot;
        net::ServerSnapshot snapshot;
        snapshot.serverTick = m_currentTick;
        snapshot.ackInputSequence = client.lastAckedInput;
        snapshot.authoritativePosition = client.lastPosition;
        snapshot.authoritativeVelocity = client.lastVelocity;
        snapshot.playerRespawned = client.respawnSnapshotTicksRemaining > 0;
        snapshot.playerDead = client.awaitingRespawn;

        const entt::entity playerEntity = resolvePlayerEntity(client);

        if (playerEntity != entt::null && m_ecsRegistry != nullptr && m_ecsRegistry->valid(playerEntity)) {
            if (const auto* health = m_ecsRegistry->try_get<ecs::HealthComponent>(playerEntity)) {
                snapshot.playerHealth = static_cast<uint16_t>(std::clamp(health->current, 0, 65535));
                snapshot.playerMaxHealth = static_cast<uint16_t>(std::clamp(health->max, 0, 65535));
            }
            if (auto* hurt = m_ecsRegistry->try_get<ecs::HurtEffectComponent>(playerEntity)) {
                snapshot.playerHurt = hurt->classicHurtEffectPending;
                if (snapshot.playerHurt && usingOwnedEcsRegistry()) {
                    hurtEventsSent.push_back(playerEntity);
                }
            }
        }

        packet.inProcessPayload = snapshot;
        client.transport->send(std::move(packet));
        if (client.respawnSnapshotTicksRemaining > 0) {
            --client.respawnSnapshotTicksRemaining;
        }
    }

    if (m_ecsRegistry != nullptr) {
        for (const entt::entity playerEntity : hurtEventsSent) {
            if (m_ecsRegistry->valid(playerEntity)) {
                if (auto* hurt = m_ecsRegistry->try_get<ecs::HurtEffectComponent>(playerEntity)) {
                    hurt->classicHurtEffectPending = false;
                }
            }
        }
    }
}

void GameServer::sendInventorySnapshotsToClients() {
    for (auto& client : m_clients) {
        if (!client.receivedHello || !client.transport || !client.transport->hasActiveRemote()) {
            continue;
        }

        net::InventorySnapshotMessage snapshot;
        if (!buildInventorySnapshot(client, snapshot)) {
            continue;
        }

        const bool changed = !client.hasLastInventorySnapshot ||
                             client.lastInventorySnapshotSelected != snapshot.selectedHotbarSlot ||
                             !inventorySnapshotSlotsEqual(client.lastInventorySnapshotSlots, snapshot.slots);
        if (!changed) {
            continue;
        }

        net::Packet packet;
        packet.channel = net::PacketChannel::ReliableWorld;
        packet.type = net::MessageType::InventorySnapshot;
        packet.inProcessPayload = snapshot;
        client.transport->send(std::move(packet));

        client.hasLastInventorySnapshot = true;
        client.lastInventorySnapshotSelected = snapshot.selectedHotbarSlot;
        client.lastInventorySnapshotSlots = std::move(snapshot.slots);
    }
}

void GameServer::sendBlockUpdatesToClients() {
    if (m_pendingBlockUpdates.empty()) {
        return;
    }

    net::Packet packet;
    packet.channel = net::PacketChannel::ReliableWorld;
    packet.type = net::MessageType::BlockUpdateBatch;
    net::BlockUpdateBatchMessage batch;
    batch.updates = std::move(m_pendingBlockUpdates);
    packet.inProcessPayload = std::move(batch);

    for (auto& client : m_clients) {
        if (!client.receivedHello || !client.transport || !client.transport->hasActiveRemote()) {
            continue;
        }
        // Send a copy to each client
        net::Packet clientPacket = packet;
        client.transport->send(std::move(clientPacket));
    }

    m_pendingBlockUpdates.clear();
}

net::BlockUpdateEntry GameServer::makeBlockOnlyUpdateEntry(const int x, const int y, const int z, const BlockID blockId) const {
    net::BlockUpdateEntry entry;
    entry.x = x;
    entry.y = y;
    entry.z = z;
    entry.blockId = static_cast<uint16_t>(blockId);
    return entry;
}

net::BlockUpdateEntry GameServer::makeSubChunkLightUpdateEntry(const int64_t chunkKey, const int scy) const {
    net::BlockUpdateEntry entry;
    const auto chunkIt = m_world.getActiveChunks().find(chunkKey);
    if (scy < 0 || scy >= Chunk::NUM_SUB_CHUNKS ||
        chunkIt == m_world.getActiveChunks().end() || !chunkIt->second) {
        return entry;
    }

    const Chunk& chunk = *chunkIt->second;
    entry.x = chunk.m_chunkX * Chunk::SIZE_X;
    entry.y = scy * SubChunk::SIZE;
    entry.z = chunk.m_chunkZ * Chunk::SIZE_Z;
    entry.blockId = kLightOnlyBlockUpdate;
    entry.packedLightPatch.resize(SubChunk::BLOCK_COUNT);
    size_t index = 0;
    const int yBase = scy * SubChunk::SIZE;
    for (int ly = 0; ly < SubChunk::SIZE; ++ly) {
        const int yPos = yBase + ly;
        for (int zPos = 0; zPos < Chunk::SIZE_Z; ++zPos) {
            for (int xPos = 0; xPos < Chunk::SIZE_X; ++xPos) {
                entry.packedLightPatch[index++] =
                    chunk.getPackedLight(xPos, yPos, zPos);
            }
        }
    }
    return entry;
}

net::BlockUpdateEntry GameServer::makeBlockUpdateEntry(const int x,
                                                       const int y,
                                                       const int z,
                                                       const BlockID blockId,
                                                       const int lightPatchRadius) const {
    net::BlockUpdateEntry entry;
    entry.x = x;
    entry.y = y;
    entry.z = z;
    entry.blockId = static_cast<uint16_t>(blockId);
    if (lightPatchRadius < 0) {
        const glm::ivec2 chunkCoords = m_world.getChunkCoords(x, z);
        return makeSubChunkLightUpdateEntry(World::chunkKey(chunkCoords.x, chunkCoords.y),
                                            Chunk::toSubChunkIndex(y));
    }
    const int patchSide = lightPatchRadius * 2 + 1;
    entry.packedLightPatch.reserve(static_cast<size_t>(patchSide * patchSide * patchSide));
    for (int dy = -lightPatchRadius; dy <= lightPatchRadius; ++dy) {
        for (int dz = -lightPatchRadius; dz <= lightPatchRadius; ++dz) {
            for (int dx = -lightPatchRadius; dx <= lightPatchRadius; ++dx) {
                entry.packedLightPatch.push_back(m_world.getPackedLight(x + dx, y + dy, z + dz));
            }
        }
    }
    return entry;
}

void GameServer::syncPlayersToClients() {
    if (m_clients.size() < 2) {
        return;
    }

    for (auto& receiver : m_clients) {
        if (!receiver.receivedHello || !receiver.transport || !receiver.transport->hasActiveRemote()) {
            continue;
        }

        for (auto& other : m_clients) {
            if (other.id == receiver.id || !other.receivedHello || !other.transport || !other.transport->hasActiveRemote()) {
                continue;
            }

            if (receiver.spawnedPlayerNetIds.insert(other.playerNetId).second) {
                net::Packet spawnPacket;
                spawnPacket.channel = net::PacketChannel::ReliableWorld;
                spawnPacket.type = net::MessageType::EntitySpawn;
                net::EntitySpawnMessage spawn;
                spawn.netId = other.playerNetId;
                spawn.kind = net::EntityKind::Player;
                spawn.position = other.lastPosition;
                spawn.velocity = other.lastVelocity;
                spawn.yaw = other.lastYaw;
                spawn.pitch = other.lastPitch;
                spawnPacket.inProcessPayload = spawn;
                receiver.transport->send(std::move(spawnPacket));
                std::printf("[Server] Sent PlayerSpawn receiver=%u sourceClient=%u netId=%u\n",
                            receiver.id,
                            other.id,
                            other.playerNetId);
                std::fflush(stdout);
            }
        }
    }

    net::EntitySnapshotMessage snapshot;
    snapshot.serverTick = m_currentTick;
    for (const auto& client : m_clients) {
        if (!client.receivedHello || !client.transport || !client.transport->hasActiveRemote()) {
            continue;
        }
        net::EntitySnapshotItem item;
        item.netId = client.playerNetId;
        item.position = client.lastPosition;
        item.velocity = client.lastVelocity;
        item.yaw = client.lastYaw;
        item.pitch = client.lastPitch;
        snapshot.entities.push_back(item);
    }
    if (snapshot.entities.empty()) {
        return;
    }

    for (auto& receiver : m_clients) {
        if (!receiver.receivedHello || !receiver.transport || !receiver.transport->hasActiveRemote()) {
            continue;
        }
        net::EntitySnapshotMessage filtered;
        filtered.serverTick = snapshot.serverTick;
        for (const auto& item : snapshot.entities) {
            if (item.netId != receiver.playerNetId) {
                filtered.entities.push_back(item);
            }
        }
        if (filtered.entities.empty()) {
            continue;
        }
        net::Packet packet;
        packet.channel = net::PacketChannel::UnreliableState;
        packet.type = net::MessageType::EntitySnapshot;
        packet.inProcessPayload = std::move(filtered);
        receiver.transport->send(std::move(packet));
    }
}

std::vector<save::PersistentEntityData> GameServer::snapshotPersistentEntities() const {
    std::vector<save::PersistentEntityData> entities;
    if (m_ecsRegistry == nullptr) {
        return entities;
    }

    const entt::registry& reg = *m_ecsRegistry;
    auto view = reg.view<ecs::MobTag, ecs::TransformComponent, ecs::HealthComponent>();
    for (const entt::entity entity : view) {
        const auto& health = view.get<ecs::HealthComponent>(entity);
        if (health.current <= 0) {
            continue;
        }

        save::PersistentEntityData data;
        data.type = "minecraft:zombie";
        const auto& transform = view.get<ecs::TransformComponent>(entity);
        data.posX = transform.position.x;
        data.posY = transform.position.y;
        data.posZ = transform.position.z;
        data.health = health.current;
        data.healthMax = health.max;

        if (const auto* physicsBody = reg.try_get<ecs::PhysicsBodyComponent>(entity)) {
            data.velX = physicsBody->body.velocity.x;
            data.velY = physicsBody->body.velocity.y;
            data.velZ = physicsBody->body.velocity.z;
        } else if (const auto* velocity = reg.try_get<ecs::VelocityComponent>(entity)) {
            data.velX = velocity->velocity.x;
            data.velY = velocity->velocity.y;
            data.velZ = velocity->velocity.z;
        }

        if (const auto* mobAI = reg.try_get<ecs::MobAIComponent>(entity)) {
            data.yaw = mobAI->yaw;
        }

        entities.push_back(data);
    }

    return entities;
}

void GameServer::savePersistentEntities() {
    if (!m_saveManager || m_ecsRegistry == nullptr) {
        return;
    }
    m_saveManager->savePersistentEntities(snapshotPersistentEntities());
}

void GameServer::restorePersistentEntities() {
    if (!m_saveManager || m_gameplayRegistry == nullptr) {
        return;
    }
    if (!m_entitiesRestorePending) {
        return;
    }
    m_entitiesRestorePending = false;

    std::vector<save::PersistentEntityData> entities;
    if (!m_saveManager->loadPersistentEntities(entities)) {
        return;
    }

    for (const save::PersistentEntityData& data : entities) {
        if (data.type != "minecraft:zombie" || data.health <= 0) {
            continue;
        }

        const glm::vec3 position(data.posX, data.posY, data.posZ);
        const entt::entity zombie = ecs::MobModelFactory::createZombie(*m_gameplayRegistry, position);
        entt::registry& reg = m_gameplayRegistry->registry();

        if (auto* health = reg.try_get<ecs::HealthComponent>(zombie)) {
            health->current = data.health;
            health->max = data.healthMax > 0 ? data.healthMax : health->max;
        }
        if (auto* ai = reg.try_get<ecs::MobAIComponent>(zombie)) {
            ai->yaw = data.yaw;
        }
        if (auto* physicsBody = reg.try_get<ecs::PhysicsBodyComponent>(zombie)) {
            physicsBody->body.velocity = glm::vec3(data.velX, data.velY, data.velZ);
        }
    }
}

bool GameServer::spawnZombieEntity(const glm::vec3& position) {
    if (m_gameplayRegistry != nullptr) {
        ecs::EntityFactory::createZombie(*m_gameplayRegistry, position);
        return true;
    }

    if (m_ecsRegistry == nullptr) {
        return false;
    }

    ecs::EntityFactory::createZombie(*m_ecsRegistry, position);
    return true;
}

net::EntitySpawnMessage GameServer::makeEntitySpawnMessage(const ecs::EntityNetId netId,
                                                           const entt::entity entity) const {
    net::EntitySpawnMessage msg;
    msg.netId = netId;

    if (!m_ecsRegistry || !m_ecsRegistry->valid(entity)) {
        msg.netId = 0;
        return msg;
    }

    const entt::registry& reg = *m_ecsRegistry;
    if (reg.all_of<ecs::DropItemTag, ecs::ItemComponent>(entity)) {
        msg.kind = net::EntityKind::Drop;
        const auto& item = reg.get<ecs::ItemComponent>(entity);
        msg.itemId = static_cast<uint16_t>(item.itemId);
        msg.stackCount = static_cast<uint16_t>(item.stackCount);
    } else if (reg.all_of<ecs::MobTag>(entity)) {
        msg.kind = net::EntityKind::Mob;
    } else if (reg.all_of<ecs::SteveTag>(entity)) {
        msg.kind = net::EntityKind::Player;
    } else {
        msg.netId = 0;
        return msg;
    }

    if (const auto* transform = reg.try_get<ecs::TransformComponent>(entity)) {
        msg.position = transform->position;
    }
    if (const auto* velocity = reg.try_get<ecs::VelocityComponent>(entity)) {
        msg.velocity = velocity->velocity;
    } else if (const auto* body = reg.try_get<ecs::PhysicsBodyComponent>(entity)) {
        msg.velocity = body->body.velocity;
    }
    if (const auto* spin = reg.try_get<ecs::SpinVisualComponent>(entity)) {
        msg.yaw = spin->yawRadians;
    }
    if (const auto* mobAI = reg.try_get<ecs::MobAIComponent>(entity)) {
        msg.yaw = mobAI->yaw;
    }
    if (const auto* camera = reg.try_get<ecs::CameraStateComponent>(entity)) {
        msg.yaw = camera->yaw;
        msg.pitch = camera->pitch;
    }

    return msg;
}

void GameServer::syncEntitiesToClients() {
    if (!m_ecsRegistry || m_clients.empty()) {
        return;
    }

    auto& reg = *m_ecsRegistry;

    // 1. Detect new entities with NetworkSyncTag that don't have EntityNetId yet
    auto newSyncView = reg.view<ecs::NetworkSyncTag>(entt::exclude<ecs::EntityNetIdComponent>);
    for (auto entity : newSyncView) {
        reg.emplace<ecs::EntityNetIdComponent>(entity, m_nextNetId);
        m_syncedEntities[m_nextNetId] = entity;
        ++m_nextNetId;
    }

    // 2. Detect despawned entities (entities in m_syncedEntities that are no longer valid)
    std::vector<ecs::EntityNetId> toDespawn;
    for (const auto& [netId, entity] : m_syncedEntities) {
        if (!reg.valid(entity)) {
            toDespawn.push_back(netId);
        }
    }

    // Send despawn messages and clean up tracking
    for (const ecs::EntityNetId netId : toDespawn) {
        for (auto& client : m_clients) {
            client.spawnedEntityNetIds.erase(netId);
        }
        for (auto& client : m_clients) {
            if (!client.receivedHello || !client.transport || !client.transport->hasActiveRemote()) {
                continue;
            }
            net::Packet packet;
            packet.channel = net::PacketChannel::ReliableWorld;
            packet.type = net::MessageType::EntityDespawn;
            net::EntityDespawnMessage msg;
            msg.netId = netId;
            packet.inProcessPayload = msg;
            client.transport->send(std::move(packet));
        }
        m_syncedEntities.erase(netId);
    }

    // 3. Send spawn messages once per connected client.
    for (auto& client : m_clients) {
        if (!client.receivedHello || !client.transport || !client.transport->hasActiveRemote()) {
            continue;
        }

        for (const auto& [netId, entity] : m_syncedEntities) {
            if (!reg.valid(entity) || client.spawnedEntityNetIds.count(netId) > 0) {
                continue;
            }

            net::EntitySpawnMessage spawn = makeEntitySpawnMessage(netId, entity);
            if (spawn.netId == 0) {
                continue;
            }

            net::Packet packet;
            packet.channel = net::PacketChannel::ReliableWorld;
            packet.type = net::MessageType::EntitySpawn;
            packet.inProcessPayload = std::move(spawn);
            client.transport->send(std::move(packet));
            client.spawnedEntityNetIds.insert(netId);
        }
    }

    // 4. Build and send entity snapshots (batch of all synced entities)
    net::EntitySnapshotMessage snapshot;
    snapshot.serverTick = m_currentTick;

    for (const auto& [netId, entity] : m_syncedEntities) {
        if (!reg.valid(entity)) continue;

        auto* transform = reg.try_get<ecs::TransformComponent>(entity);
        if (!transform) continue;

        net::EntitySnapshotItem item;
        item.netId = netId;
        item.position = transform->position;

        auto* velocity = reg.try_get<ecs::VelocityComponent>(entity);
        if (velocity) {
            item.velocity = velocity->velocity;
        }

        auto* spin = reg.try_get<ecs::SpinVisualComponent>(entity);
        if (spin) {
            item.yaw = spin->yawRadians;
        }
        auto* mobAI = reg.try_get<ecs::MobAIComponent>(entity);
        if (mobAI) {
            item.yaw = mobAI->yaw;
        }

        snapshot.entities.push_back(item);
    }

    if (!snapshot.entities.empty()) {
        net::Packet packet;
        packet.channel = net::PacketChannel::UnreliableState;
        packet.type = net::MessageType::EntitySnapshot;
        packet.inProcessPayload = std::move(snapshot);

        for (auto& client : m_clients) {
            if (!client.receivedHello || !client.transport || !client.transport->hasActiveRemote()) {
                continue;
            }
            net::Packet clientPacket = packet;
            client.transport->send(std::move(clientPacket));
        }
    }
}

void GameServer::sendChunkDataToClient(ConnectedClient& client, int cx, int cz) {
    const int64_t key = World::chunkKey(cx, cz);
    const auto& activeChunks = m_world.getActiveChunks();
    auto it = activeChunks.find(key);
    if (it == activeChunks.end() || !it->second) {
        return;
    }

    net::Packet packet;
    packet.channel = net::PacketChannel::ReliableWorld;
    packet.type = net::MessageType::ChunkData;
    net::ChunkDataMessage data;
    data.chunkX = cx;
    data.chunkZ = cz;
    data.revision = static_cast<uint32_t>(m_world.getActiveChunkRevision());
    data.chunk = it->second;  // Zero-copy: share the Chunk pointer
    packet.inProcessPayload = std::move(data);
    client.transport->send(std::move(packet));

    ++client.totalChunksSent;
    if (client.chunkSendLogCount < 12 || client.totalChunksSent % 25 == 0) {
        std::printf("[Server] Sent ChunkData client=%u chunk=(%d,%d) total=%d active=%zu\n",
                    client.id,
                    cx,
                    cz,
                    client.totalChunksSent,
                    m_world.getActiveChunks().size());
        std::fflush(stdout);
        ++client.chunkSendLogCount;
    }
}

void GameServer::checkSpawnChunksReady() {
    // Consider spawn chunks ready when a 5x5 area around (0,0) is loaded
    constexpr int kSpawnRadius = 2;
    int loadedCount = 0;
    const int totalNeeded = (2 * kSpawnRadius + 1) * (2 * kSpawnRadius + 1);

    for (int dx = -kSpawnRadius; dx <= kSpawnRadius; ++dx) {
        for (int dz = -kSpawnRadius; dz <= kSpawnRadius; ++dz) {
            const int64_t key = World::chunkKey(dx, dz);
            if (m_world.getActiveChunks().count(key) > 0) {
                ++loadedCount;
            }
        }
    }

    if (loadedCount >= totalNeeded) {
        m_spawnChunksReady = true;
    }
}

} // namespace server
