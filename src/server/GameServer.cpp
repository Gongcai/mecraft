#include "GameServer.h"
#include "../Diagnostics.h"
#include "../world/World.h"
#include "../world/WeatherSystem.h"
#include "../world/block/Block.h"
#include "../world/block/BedBlock.h"
#include "../world/block/DoorBlock.h"
#include "../world/block/PistonBlock.h"
#include "../world/block/Placement.h"
#include "../thread/ThreadPool.h"
#include "../save/SaveManager.h"
#include "../ecs/GameplayPipeline.h"
#include "../ecs/GameplayRegistry.h"
#include "../ecs/entity/EntityFactory.h"
#include "../ecs/systems/item/ItemSpawnSystem.h"
#include "../ecs/systems/world/BlockSupportSystem.h"
#include "../ecs/systems/world/FallingBlockSpawnSystem.h"
#include "../ecs/systems/world/FallingBlockTickSystem.h"
#include "../ecs/systems/world/FarmlandMoistureSystem.h"
#include "../ecs/systems/world/HopperSystem.h"
#include "../ecs/systems/world/PressurePlateSystem.h"
#include "../ecs/systems/world/RandomTickSystem.h"
#include "../ecs/systems/world/RedstoneDeviceActionSystem.h"
#include "../ecs/systems/world/RedstoneSystem.h"
#include "../ecs/components/Components.h"
#include "../ecs/components/NetworkComponents.h"
#include "../game/interaction/BlockInteractionDispatcher.h"
#include "../game/inventory/BlockEntityInventoryStore.h"
#include "../game/inventory/BlockEntityInventoryLifecycle.h"
#include "../game/inventory/ContainerBehaviorRegistry.h"
#include "../game/inventory/MachineInventoryLifecycle.h"
#include "../game/inventory/MachineInventoryStore.h"
#include "../game/inventory/SmeltingProcessorRuntime.h"
#include "../item/Item.h"
#include "../item/ItemUseDispatcher.h"
#include "../physics/PhysicsSystem.h"
#include "../ui/inventory/ContainerUiRegistry.h"
#include "../world/block/BlockStateRegistry.h"
#include "../world/fluid/FluidState.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <functional>
#include <stdexcept>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace server {
namespace {
constexpr net::EntityNetId kPlayerNetIdBase = 0x80000000u;
constexpr uint32_t kLightOnlyBlockUpdate = net::LIGHT_ONLY_BLOCK_UPDATE_STATE_ID;
constexpr int kPlayerRespawnSnapshotRepeatTicks = 5;
constexpr int kMaxClientViewDistance = 32;
constexpr float kPlayerPoseSyncEpsilonSq = 0.000001f;
constexpr float kPlayerPoseCorrectionAcceptDistanceSq = 0.1225f;

void clearOpenContainerSession(ConnectedClient& client);

int blockToChunkCoord(const int value) {
    return static_cast<int>(std::floor(static_cast<float>(value) / static_cast<float>(Chunk::SIZE_X)));
}

int64_t blockUpdateChunkKey(const net::BlockUpdateEntry& update) {
    return World::chunkKey(blockToChunkCoord(update.x), blockToChunkCoord(update.z));
}

float distanceSquared(const glm::vec3& a, const glm::vec3& b) {
    const glm::vec3 delta = a - b;
    return delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
}

bool poseValueChanged(const glm::vec3& a, const glm::vec3& b) {
    return distanceSquared(a, b) > kPlayerPoseSyncEpsilonSq;
}

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

std::string normalizeEntityCommandId(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    return value.find(':') == std::string::npos ? "minecraft:" + value : value;
}

struct BlockEntityPositionKey {
    int x = 0;
    int y = 0;
    int z = 0;

    [[nodiscard]] bool operator==(const BlockEntityPositionKey& other) const {
        return x == other.x && y == other.y && z == other.z;
    }
};

struct BlockEntityPositionHash {
    [[nodiscard]] std::size_t operator()(const BlockEntityPositionKey& key) const {
        const std::size_t hx = std::hash<int>{}(key.x);
        const std::size_t hy = std::hash<int>{}(key.y);
        const std::size_t hz = std::hash<int>{}(key.z);
        return hx ^ (hy + 0x9e3779b9u + (hx << 6u) + (hx >> 2u)) ^
               (hz + 0x9e3779b9u + (hy << 6u) + (hy >> 2u));
    }
};

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

void collectEntityTree(entt::registry& registry, const entt::entity entity, std::vector<entt::entity>& out) {
    if (entity == entt::null || !registry.valid(entity)) {
        return;
    }

    out.push_back(entity);
    if (const auto* children = registry.try_get<ecs::ChildrenComponent>(entity)) {
        for (const entt::entity child : children->children) {
            collectEntityTree(registry, child, out);
        }
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

net::InventorySlotData slotDataFromStack(const ItemStack& stack) {
    net::InventorySlotData slot;
    if (!stack.isEmpty()) {
        slot.itemId = static_cast<uint16_t>(std::clamp<uint32_t>(stack.itemId, 0u, 0xFFFFu));
        slot.stackCount = static_cast<uint8_t>(std::clamp<uint32_t>(stack.count, 0u, 0xFFu));
    }
    return slot;
}

ItemStack stackFromSlotData(const net::InventorySlotData& slot) {
    ItemStack stack;
    if (slot.itemId != 0 && slot.stackCount != 0) {
        stack.itemId = static_cast<ItemID>(slot.itemId);
        stack.count = slot.stackCount;
    }
    return stack;
}

bool containerSnapshotsEqual(const net::ContainerSnapshotMessage& a,
                             const net::ContainerSnapshotMessage& b) {
    return a.containerId == b.containerId &&
           a.containerUiId == b.containerUiId &&
           a.behaviorId == b.behaviorId &&
           a.blockPosition == b.blockPosition &&
           inventorySnapshotSlotsEqual(a.containerSlots, b.containerSlots) &&
           inventorySnapshotSlotsEqual(a.playerSlots, b.playerSlots) &&
           a.cursor.itemId == b.cursor.itemId &&
           a.cursor.stackCount == b.cursor.stackCount &&
           a.burnFraction == b.burnFraction &&
           a.cookFraction == b.cookFraction;
}

bool persistentBlockEntityTypeForBlock(const BlockID blockId, std::string& outType) {
    if (blockId == RUNTIME_ID_NULL) {
        return false;
    }

    const BlockDef& blockDef = BlockRegistry::getFast(blockId);
    if (blockDef.containerUi.empty()) {
        return false;
    }

    const ui::ContainerUiDef& uiDef = ui::ContainerUiRegistry::require(blockDef.containerUi);
    const ContainerBehaviorDef& behavior = ContainerBehaviorRegistry::require(uiDef.behavior);
    if (behavior.handler != "storage" && behavior.handler != "smelting") {
        return false;
    }
    if (behavior.storage.kind != ContainerStorageKind::BlockEntity) {
        throw std::runtime_error(behavior.id + " persistent block entity requires block_entity storage");
    }

    outType = behavior.id;
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
    if (registry != nullptr && m_blockEntitiesRestorePending) {
        restoreBlockEntities();
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
    if (registry != nullptr && m_blockEntitiesRestorePending) {
        restoreBlockEntities();
    }
}

void GameServer::ensureOwnedEcsRuntime() {
    if (!m_ownedGameplayRegistry) {
        m_ownedGameplayRegistry = std::make_unique<ecs::GameplayRegistry>();
    }
    if (!m_ownedPhysicsSystem) {
        m_ownedPhysicsSystem = std::make_unique<physics::PhysicsSystem>(&m_world);
    }
    if (!m_ownedGameplayPipeline) {
        m_ownedGameplayPipeline = std::make_unique<ecs::GameplayPipeline>(ecs::GameplayPipelineProfile::Server);
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
    client.playerPoseCorrectionPending = false;
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

bool GameServer::buildContainerSnapshot(const ConnectedClient& client,
                                        net::ContainerSnapshotMessage& out) const {
    if (m_ecsRegistry == nullptr || client.openContainerId == 0) {
        return false;
    }

    const StateID state = m_world.getBlockState(client.openContainerPosition.x,
                                                client.openContainerPosition.y,
                                                client.openContainerPosition.z);
    if (state == RUNTIME_ID_NULL) {
        return false;
    }

    const BlockID blockId = BlockStateRegistry::getBlockId(state);
    const BlockDef& blockDef = BlockRegistry::getFast(blockId);
    if (blockDef.containerUi != client.openContainerUiId) {
        return false;
    }

    const ui::ContainerUiDef& uiDef = ui::ContainerUiRegistry::require(client.openContainerUiId);
    const ContainerBehaviorDef& behavior = ContainerBehaviorRegistry::require(uiDef.behavior);
    if (behavior.id != client.openContainerBehaviorId) {
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

    out = {};
    out.containerId = client.openContainerId;
    out.containerUiId = client.openContainerUiId;
    out.behaviorId = client.openContainerBehaviorId;
    out.blockPosition = client.openContainerPosition;
    out.cursor = client.openContainerCursor;

    out.playerSlots.reserve(Inventory::INVENTORY_SIZE);
    for (int slot = 0; slot < Inventory::INVENTORY_SIZE; ++slot) {
        out.playerSlots.push_back(slotDataFromStack(inventoryData->inventory.getSlotStack(slot)));
    }

    if (behavior.handler == "storage") {
        if (!m_ecsRegistry->ctx().contains<BlockEntityInventoryStore>()) {
            return false;
        }
        const BlockEntityInventoryStore& store = m_ecsRegistry->ctx().get<BlockEntityInventoryStore>();
        const BlockEntityInventory* inventory = store.find(client.openContainerPosition);
        if (inventory == nullptr) {
            return false;
        }
        out.containerSlots.reserve(static_cast<std::size_t>(behavior.storage.slots));
        for (int slot = 0; slot < behavior.storage.slots; ++slot) {
            out.containerSlots.push_back(slotDataFromStack(inventory->getSlotStack(slot)));
        }
        return true;
    }

    if (behavior.handler == "smelting") {
        if (!m_ecsRegistry->ctx().contains<MachineInventoryStore>()) {
            return false;
        }
        const MachineInventoryStore& store = m_ecsRegistry->ctx().get<MachineInventoryStore>();
        const MachineInventory* machine = store.find(client.openContainerPosition);
        if (machine == nullptr) {
            return false;
        }
        out.containerSlots.reserve(static_cast<std::size_t>(machine->slotCount()));
        for (int slot = 0; slot < machine->slotCount(); ++slot) {
            out.containerSlots.push_back(slotDataFromStack(machine->getSlotStack(slot)));
        }
        out.burnFraction = machine->burnFraction();
        out.cookFraction = machine->cookFraction();
        return true;
    }

    return false;
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

    bool droppedAny = false;
    for (int slot = 0; slot < Inventory::INVENTORY_SIZE; ++slot) {
        const ItemStack stack = inventoryData->inventory.getSlotStack(slot);
        if (stack.isEmpty()) {
            continue;
        }
        ecs::ItemSpawnSystem::spawnAtPosition(*m_gameplayRegistry, stack.itemId, position, stack.count);
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
    client.playerPoseCorrectionPending = false;
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

        auto& inventoryState = reg.get<ecs::InventoryComponent>(client.ecsPlayerEntity);
        inventoryState.selectedHotbarSlot = std::clamp(static_cast<int>(client.selectedHotbarSlot),
                                                       0,
                                                       Inventory::HOTBAR_SIZE - 1);

        auto& playerMode = reg.get<ecs::PlayerModeComponent>(client.ecsPlayerEntity);
        playerMode.creative = client.gameplayMode == net::NetworkGameplayMode::Creative;

        auto& blockIntent = reg.get<ecs::BlockActionIntentComponent>(client.ecsPlayerEntity);
        blockIntent.wantsBreak = hasInputAction(client.pendingInputActions, net::ClientInputActions::Attack);
        blockIntent.wantsPlace = hasInputAction(client.pendingInputActions, net::ClientInputActions::UseItem);
    }
}

void GameServer::syncClientsFromPlayerProxies() {
    if (m_ecsRegistry == nullptr) {
        return;
    }

    auto& reg = *m_ecsRegistry;
    for (auto& client : m_clients) {
        const bool active = client.receivedHello &&
                            client.transport &&
                            client.transport->hasActiveRemote();
        if (!active || client.awaitingRespawn || client.respawnSnapshotTicksRemaining > 0) {
            continue;
        }

        const entt::entity playerEntity = resolvePlayerEntity(client);
        if (playerEntity == entt::null || !reg.valid(playerEntity)) {
            continue;
        }

        const auto* transform = reg.try_get<ecs::TransformComponent>(playerEntity);
        if (transform == nullptr) {
            continue;
        }

        glm::vec3 velocity = client.lastVelocity;
        if (const auto* physicsBody = reg.try_get<ecs::PhysicsBodyComponent>(playerEntity)) {
            velocity = physicsBody->body.velocity;
        } else if (const auto* velocityComponent = reg.try_get<ecs::VelocityComponent>(playerEntity)) {
            velocity = velocityComponent->velocity;
        }

        const bool poseChanged = poseValueChanged(client.lastPosition, transform->position) ||
                                 poseValueChanged(client.lastVelocity, velocity);
        client.lastPosition = transform->position;
        client.lastVelocity = velocity;
        if (poseChanged) {
            client.playerPoseCorrectionPending = true;
        }
    }
}

void GameServer::tickServerEcs(const float dt) {
    if (!usingOwnedEcsRegistry() || m_gameplayRegistry == nullptr || m_ownedGameplayPipeline == nullptr) {
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

    ecs::GameplayPipelineHooks hooks;
    hooks.afterDamageSystem = [this, dt](ecs::SystemContext&) {
        updatePlayerLifecycle(dt);
    };
    m_ownedGameplayPipeline->runFixedUpdate(*m_gameplayRegistry, services, dt, m_currentTick, &hooks);

    for (auto& client : m_clients) {
        client.pendingInputActions = 0;
    }
}

void GameServer::init(uint32_t seed, ThreadPool* threadPool, int renderDistance) {
    m_world.setRenderDistance(renderDistance);
    m_world.setThreadPool(threadPool);
    m_world.init(seed);

    // Register block change callback to collect dirty blocks for BlockUpdateBatch
    m_world.setBlockChangeCallback([this](int x, int y, int z, StateID newStateId) {
        m_pendingBlockUpdates.push_back(makeBlockOnlyUpdateEntry(x, y, z, newStateId));
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
    MECRAFT_LOG_PRINTF("[Server] World initialized seed=%u renderDistance=%d spawn=(%.1f, %.1f, %.1f)\n",
                       seed,
                       renderDistance,
                       m_spawnPosition.x,
                       m_spawnPosition.y,
                       m_spawnPosition.z);
    MECRAFT_LOG_FLUSH(stdout);
    ensureOwnedEcsRuntime();
    m_entitiesRestorePending = m_saveManager != nullptr;
    m_blockEntitiesRestorePending = m_saveManager != nullptr;
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
            // Restore the world's default gameplay mode (creative/survival) so
            // it is applied to clients on connect and persists across sessions.
            net::NetworkGameplayMode savedMode = net::NetworkGameplayMode::Survival;
            parseGameplayMode(meta.gameMode, savedMode);
            m_defaultGameplayMode = savedMode;
            MECRAFT_LOG_PRINTF("[Server] Loaded existing world (seed=%u, mode=%s)\n",
                               seed,
                               modeName(m_defaultGameplayMode));
        } else {
            // New world - set creation timestamp
            meta.seed = seed;
            meta.createdUtc = save::SaveManager::currentUtcTimestamp();
            meta.lastSavedUtc = meta.createdUtc;
            meta.displayName = displayName.empty() ? "New World" : displayName;
            m_saveManager->saveLevelMeta(meta);
            m_loadedMeta = meta;
            MECRAFT_LOG_PRINTF("[Server] Created new world (seed=%u)\n", seed);
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
        saveBlockEntities();
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
    // Persist the current gameplay mode so it is restored next session.
    meta.gameMode = modeName(currentGameplayMode());

    m_saveManager->saveLevelMeta(meta);
    m_loadedMeta = meta;
}

net::NetworkGameplayMode GameServer::currentGameplayMode() const {
    // Prefer the first connected client's authoritative mode; fall back to the
    // world default when no client is online (e.g. shutting down).
    for (const auto& client : m_clients) {
        if (client.receivedHello) {
            return client.gameplayMode;
        }
    }
    return m_defaultGameplayMode;
}

void GameServer::acceptClient(std::unique_ptr<net::ITransportEndpoint> transport, net::ClientId id) {
    ConnectedClient client;
    client.id = id;
    client.transport = std::move(transport);
    client.lastPosition = m_spawnPosition;
    client.playerNetId = kPlayerNetIdBase | id;
    client.isAdmin = id == 1;
    client.viewDistance = m_world.getRenderDistance();
    client.gameplayMode = m_defaultGameplayMode;  // restored from level.json at init
    m_clients.push_back(std::move(client));
    MECRAFT_LOG_PRINTF("[Server] Accepted transport slot for client %u admin=%d\n", id, id == 1 ? 1 : 0);
    MECRAFT_LOG_FLUSH(stdout);
}

void GameServer::tick(float dt) {
    // Process incoming client messages first so block edits and player poses
    // participate in this tick's world/light update before snapshots are sent.
    processClientMessages();
    cleanupDisconnectedClients();
    if (usingOwnedEcsRegistry()) {
        syncOwnedPlayerProxies();
    }
    tickWorldSystems();
    syncClientsFromPlayerProxies();

    // Periodic autosave
    if (m_saveManager) {
        m_autosaveTimer += dt;
        if (m_autosaveTimer >= AUTOSAVE_INTERVAL_SECONDS) {
            m_autosaveTimer = 0.0f;
            m_world.flushSaves();
            savePersistentEntities();
            saveBlockEntities();
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

    for (const auto& client : m_clients) {
        if (client.receivedHello) {
            m_world.flushInteractiveLighting(client.lastPosition);
        }
    }
    m_world.update(loadCenter, dt);
    tickServerEcs(dt);
    syncClientsFromPlayerProxies();
    if (!usingOwnedEcsRegistry()) {
        updatePlayerLifecycle(dt);
        syncClientsFromPlayerProxies();
    }

    // Send new chunks to clients
    sendNewChunksToClients();

    // Send authoritative snapshots to clients
    sendSnapshotsToClients();
    sendInventorySnapshotsToClients();
    sendContainerSnapshotsToClients();

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
    sendContainerSnapshotsToClients();
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
    ecs::FarmlandMoistureSystem::hydrateLoadedFarmland(m_world);
    ecs::RandomTickSystem::processWorld(m_world, m_currentTick);
    if (m_gameplayRegistry != nullptr) {
        ecs::BlockSupportSystem::processWorldQueue(m_world, *m_gameplayRegistry, 1024);
        ecs::FallingBlockSpawnSystem::processEvents(*m_gameplayRegistry);
        ecs::FallingBlockTickSystem::tickWorld(m_world, *m_gameplayRegistry);
    } else {
        ecs::BlockSupportSystem::processWorldQueue(m_world, 1024);
    }
    if (m_gameplayRegistry != nullptr) {
        ecs::PressurePlateSystem::processWorldEntities(m_world, *m_gameplayRegistry);
    }
    if ((m_currentTick % 2u) == 0u) {
        if (m_gameplayRegistry != nullptr) {
            ecs::RedstoneSystem::processWorld(m_world, m_currentTick / 2u, *m_gameplayRegistry, 4096);
            ecs::RedstoneDeviceActionSystem::processEvents(m_world, *m_gameplayRegistry);
        } else {
            ecs::RedstoneSystem::processWorld(m_world, m_currentTick / 2u, 4096);
        }
    }
    if (m_gameplayRegistry != nullptr) {
        ecs::HopperSystem::processWorld(m_world, *m_gameplayRegistry, m_currentTick);
    }
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
                clearOpenContainerSession(client);
                client.chunkSendLogCount = 0;
                client.totalChunksSent = 0;
                if (packet.inProcessPayload.has_value()) {
                    const auto& hello = std::any_cast<const net::ClientHello&>(packet.inProcessPayload);
                    MECRAFT_LOG_PRINTF("[Server] ClientHello client=%u protocol=%u\n",
                                       client.id,
                                       hello.protocolVersion);
                } else {
                    MECRAFT_LOG_PRINTF("[Server] ClientHello client=%u without decoded payload\n", client.id);
                }
                MECRAFT_LOG_FLUSH(stdout);

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
                    client.selectedHotbarSlot = static_cast<uint8_t>(
                        std::clamp(static_cast<int>(input.selectedHotbarSlot), 0, Inventory::HOTBAR_SIZE - 1));
                    if (!client.awaitingRespawn && client.respawnSnapshotTicksRemaining <= 0) {
                        const bool acceptsReportedPose =
                            !client.playerPoseCorrectionPending ||
                            distanceSquared(input.playerPosition, client.lastPosition) <=
                                kPlayerPoseCorrectionAcceptDistanceSq;
                        if (acceptsReportedPose) {
                            client.lastPosition = input.playerPosition;
                            client.lastVelocity = input.playerVelocity;
                            client.playerPoseCorrectionPending = false;
                        }
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
                    client.viewDistance = std::clamp(config.renderDistance, 1, kMaxClientViewDistance);
                    if (client.viewDistance > m_world.getRenderDistance()) {
                        m_world.setRenderDistance(client.viewDistance);
                    }
                    MECRAFT_LOG_PRINTF("[Server] ClientViewConfig client=%u renderDistance=%d worldRenderDistance=%d\n",
                                       client.id,
                                       client.viewDistance,
                                       m_world.getRenderDistance());
                    MECRAFT_LOG_FLUSH(stdout);
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
            case net::MessageType::ClientContainerOpenRequest: {
                if (packet.inProcessPayload.has_value()) {
                    const auto& request =
                        std::any_cast<const net::ClientContainerOpenRequest&>(packet.inProcessPayload);
                    if (!client.awaitingRespawn && client.respawnSnapshotTicksRemaining <= 0) {
                        handleClientContainerOpenRequest(client, request);
                    }
                }
                break;
            }
            case net::MessageType::ClientContainerSlotAction: {
                if (packet.inProcessPayload.has_value()) {
                    const auto& action =
                        std::any_cast<const net::ClientContainerSlotAction&>(packet.inProcessPayload);
                    if (!client.awaitingRespawn && client.respawnSnapshotTicksRemaining <= 0) {
                        handleClientContainerSlotAction(client, action);
                    }
                }
                break;
            }
            case net::MessageType::ClientContainerClose: {
                if (packet.inProcessPayload.has_value()) {
                    const auto& close = std::any_cast<const net::ClientContainerClose&>(packet.inProcessPayload);
                    handleClientContainerClose(client, close);
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
            MECRAFT_LOG_PRINTF("[Server] Removing disconnected client %u netId=%u\n",
                               client.id,
                               client.playerNetId);
            MECRAFT_LOG_FLUSH(stdout);
            broadcastPlayerDespawn(client.playerNetId, client.id);
        }
        clearOpenContainerSession(client);
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

        MECRAFT_LOG_PRINTF("[Server] Client %u lost remote peer netId=%u\n",
                           client.id,
                           client.playerNetId);
        MECRAFT_LOG_FLUSH(stdout);
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
        clearOpenContainerSession(client);
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
        client.selectedHotbarSlot = 0;
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
                          "Commands: /help, /list, /gamemode <survival|creative> [clientId], /give <item> [count] [clientId], /time set <0..1200>, /weather <clear|rain|storm|snow>, /summon <entity>");
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

    if (primary == "give") {
        std::string itemName;
        iss >> itemName;
        if (itemName.empty()) {
            sendCommandResult(client, request.sequence, false, "Usage: /give <item> [count] [clientId]");
            return;
        }

        uint32_t count = 1;
        std::string countStr;
        iss >> countStr;
        if (!countStr.empty()) {
            char* endPtr = nullptr;
            const unsigned long parsed = std::strtoul(countStr.c_str(), &endPtr, 10);
            if (endPtr == countStr.c_str() || *endPtr != '\0' || parsed == 0 || parsed > 0xFFFFul) {
                sendCommandResult(client, request.sequence, false, "Usage: /give <item> [count] [clientId]");
                return;
            }
            count = static_cast<uint32_t>(parsed);
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

        ItemID itemId = RUNTIME_ID_NULL;
        if (!ItemRegistry::tryGetIdByName(itemName, itemId) || itemId == RUNTIME_ID_NULL) {
            sendCommandResult(client, request.sequence, false, "Unknown item: " + itemName);
            return;
        }

        ConnectedClient* target = findClient(targetId);
        if (!target || !target->receivedHello) {
            sendCommandResult(client, request.sequence, false, "Target client is not online.");
            return;
        }
        if (m_ecsRegistry == nullptr) {
            sendCommandResult(client, request.sequence, false, "Inventory system is not available.");
            return;
        }

        const entt::entity playerEntity = resolvePlayerEntity(*target);
        if (playerEntity == entt::null || !m_ecsRegistry->valid(playerEntity)) {
            sendCommandResult(client, request.sequence, false, "Target inventory is not available.");
            return;
        }

        auto* inventoryData = m_ecsRegistry->try_get<ecs::InventoryDataComponent>(playerEntity);
        if (inventoryData == nullptr) {
            sendCommandResult(client, request.sequence, false, "Target inventory is not available.");
            return;
        }

        const uint32_t remaining = inventoryData->inventory.addItem(itemId, count);
        const uint32_t added = count - remaining;
        if (added == 0) {
            sendCommandResult(client, request.sequence, false, "Inventory is full.");
            return;
        }

        target->hasLastInventorySnapshot = false;
        const std::string message = "Gave " + std::to_string(added) + " " +
                                    ItemRegistry::getNamespacedId(itemId).full() +
                                    " to " + playerName(target->id) + ".";
        sendCommandResult(client, request.sequence, true, message);
        sendInventorySnapshotsToClients();
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
        const std::string entityId = normalizeEntityCommandId(entityType);
        if (entityId.empty()) {
            sendCommandResult(client, request.sequence, false, "Usage: /summon <entity>");
            return;
        }

        constexpr float kSummonDistance = 2.0f;
        constexpr float kDegreesToRadians = 0.017453292519943295f;
        const float yawRad = client.lastYaw * kDegreesToRadians;
        const glm::vec3 forward(std::cos(yawRad), 0.0f, std::sin(yawRad));
        const glm::vec3 basePosition = client.receivedHello ? client.lastPosition : m_spawnPosition;
        const glm::vec3 spawnPosition = basePosition + forward * kSummonDistance;

        if (!spawnMobEntity(entityId, spawnPosition)) {
            sendCommandResult(client, request.sequence, false, "Unknown or unsupported entity: " + entityId);
            return;
        }

        const std::string message = "Summoned " + entityType + ".";
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

namespace {

void replaceSelectedServerItem(ecs::InventoryDataComponent& inventoryData, const ItemID itemId) {
    ItemStack replacement;
    replacement.itemId = itemId;
    replacement.count = 1;
    replacement.durability = 0;
    inventoryData.inventory.setSlotStack(inventoryData.inventory.getSelectedSlot(), replacement);
}

BlockID removeServerTargetBlock(World& world,
                                const glm::ivec3& hitBlock,
                                std::vector<glm::ivec3>& removedPositions) {
    const StateID targetState = world.getBlockState(hitBlock.x, hitBlock.y, hitBlock.z);
    if (BedBlockLogic::isBedState(targetState)) {
        return BedBlockLogic::removeBed(world, hitBlock, &removedPositions);
    }
    if (DoorBlockLogic::isDoorState(targetState)) {
        return DoorBlockLogic::removeDoor(world, hitBlock, &removedPositions);
    }
    if (PistonBlockLogic::isPistonAssemblyState(targetState)) {
        return PistonBlockLogic::removePistonAssembly(world, hitBlock, &removedPositions);
    }

    world.setBlock(hitBlock.x, hitBlock.y, hitBlock.z, RUNTIME_ID_NULL);
    removedPositions.push_back(hitBlock);
    return BlockStateRegistry::getBlockId(targetState);
}

uint32_t addToStack(ItemStack& target, const ItemID itemId, const uint32_t count) {
    if (itemId == 0 || count == 0) {
        return count;
    }

    const ItemDef& def = ItemRegistry::get(itemId);
    if (def.maxStack == 0) {
        return count;
    }

    if (target.isEmpty()) {
        const uint16_t add = static_cast<uint16_t>(std::min<uint32_t>(count, def.maxStack));
        target.itemId = itemId;
        target.count = add;
        target.durability = def.isTool ? def.maxDurability : 0;
        return count - add;
    }

    if (target.itemId != itemId || target.count >= def.maxStack) {
        return count;
    }

    const uint16_t freeSpace = static_cast<uint16_t>(def.maxStack - target.count);
    const uint16_t add = static_cast<uint16_t>(std::min<uint32_t>(count, freeSpace));
    target.count = static_cast<uint16_t>(target.count + add);
    return count - add;
}

void clearOpenContainerSession(ConnectedClient& client) {
    client.openContainerId = 0;
    client.openContainerPosition = glm::ivec3(0);
    client.openContainerUiId.clear();
    client.openContainerBehaviorId.clear();
    client.openContainerCursor = {};
    client.hasLastContainerSnapshot = false;
    client.lastContainerSnapshot = {};
}

} // namespace

void GameServer::handleClientContainerOpenRequest(ConnectedClient& client,
                                                  const net::ClientContainerOpenRequest& request) {
    if (!ItemUseDispatcher::isWithinReach(request.playerPosition, request.blockPosition)) {
        return;
    }
    if (m_ecsRegistry == nullptr) {
        return;
    }

    if (client.openContainerId != 0) {
        handleClientContainerClose(client, net::ClientContainerClose{client.openContainerId});
    }

    const StateID state = m_world.getBlockState(request.blockPosition.x,
                                                request.blockPosition.y,
                                                request.blockPosition.z);
    if (state == RUNTIME_ID_NULL) {
        return;
    }
    const BlockID blockId = BlockStateRegistry::getBlockId(state);
    const BlockDef& blockDef = BlockRegistry::getFast(blockId);
    if (blockDef.containerUi.empty()) {
        return;
    }

    const ui::ContainerUiDef& uiDef = ui::ContainerUiRegistry::require(blockDef.containerUi);
    const ContainerBehaviorDef& behavior = ContainerBehaviorRegistry::require(uiDef.behavior);
    if (behavior.storage.kind != ContainerStorageKind::BlockEntity) {
        throw std::runtime_error(behavior.id + " network container requires block_entity storage");
    }

    if (behavior.handler == "storage") {
        BlockEntityInventoryStore& store = m_ecsRegistry->ctx().contains<BlockEntityInventoryStore>()
            ? m_ecsRegistry->ctx().get<BlockEntityInventoryStore>()
            : m_ecsRegistry->ctx().emplace<BlockEntityInventoryStore>();
        static_cast<void>(store.getOrCreate(request.blockPosition, behavior.id, behavior.storage.slots));
    } else if (behavior.handler == "smelting") {
        MachineInventoryStore& store = m_ecsRegistry->ctx().contains<MachineInventoryStore>()
            ? m_ecsRegistry->ctx().get<MachineInventoryStore>()
            : m_ecsRegistry->ctx().emplace<MachineInventoryStore>();
        static_cast<void>(store.getOrCreate(request.blockPosition, behavior.id, behavior.storage.slots));
    } else {
        return;
    }

    client.lastPosition = request.playerPosition;
    client.openContainerId = m_nextContainerId++;
    if (client.openContainerId == 0) {
        client.openContainerId = m_nextContainerId++;
    }
    client.openContainerPosition = request.blockPosition;
    client.openContainerUiId = uiDef.id;
    client.openContainerBehaviorId = behavior.id;
    client.openContainerCursor = {};
    client.hasLastContainerSnapshot = false;
}

void GameServer::handleClientContainerSlotAction(ConnectedClient& client,
                                                 const net::ClientContainerSlotAction& action) {
    if (m_ecsRegistry == nullptr ||
        client.openContainerId == 0 ||
        action.containerId != client.openContainerId) {
        return;
    }

    const entt::entity playerEntity = resolvePlayerEntity(client);
    if (playerEntity == entt::null || !m_ecsRegistry->valid(playerEntity)) {
        return;
    }
    auto* inventoryData = m_ecsRegistry->try_get<ecs::InventoryDataComponent>(playerEntity);
    if (inventoryData == nullptr) {
        return;
    }

    const ui::ContainerUiDef& uiDef = ui::ContainerUiRegistry::require(client.openContainerUiId);
    const ContainerBehaviorDef& behavior = ContainerBehaviorRegistry::require(uiDef.behavior);

    BlockEntityInventory* storage = nullptr;
    MachineInventory* machine = nullptr;
    SmeltingProcessorRuntime smeltingRuntime;
    SmeltingSystem* smeltingSystem = nullptr;
    int containerSlotCount = 0;

    if (behavior.handler == "storage") {
        if (!m_ecsRegistry->ctx().contains<BlockEntityInventoryStore>()) {
            return;
        }
        BlockEntityInventoryStore& store = m_ecsRegistry->ctx().get<BlockEntityInventoryStore>();
        storage = &store.getOrCreate(client.openContainerPosition, behavior.id, behavior.storage.slots);
        containerSlotCount = behavior.storage.slots;
    } else if (behavior.handler == "smelting") {
        if (!m_ecsRegistry->ctx().contains<MachineInventoryStore>() ||
            !m_ecsRegistry->ctx().contains<SmeltingSystem>()) {
            return;
        }
        MachineInventoryStore& store = m_ecsRegistry->ctx().get<MachineInventoryStore>();
        machine = &store.getOrCreate(client.openContainerPosition, behavior.id, behavior.storage.slots);
        containerSlotCount = machine->slotCount();
        smeltingRuntime = SmeltingProcessorRuntime::create(uiDef, behavior, containerSlotCount);
        smeltingSystem = &m_ecsRegistry->ctx().get<SmeltingSystem>();
    } else {
        return;
    }

    const auto validSlot = [containerSlotCount](const net::ContainerSlotSpace space, const int slot) {
        if (space == net::ContainerSlotSpace::Player) {
            return slot >= 0 && slot < Inventory::INVENTORY_SIZE;
        }
        if (space == net::ContainerSlotSpace::Container) {
            return slot >= 0 && slot < containerSlotCount;
        }
        return false;
    };
    if (!validSlot(action.slotSpace, action.slot)) {
        return;
    }

    const auto getSlot = [&](const net::ContainerSlotSpace space, const int slot) -> ItemStack {
        if (space == net::ContainerSlotSpace::Player) {
            return inventoryData->inventory.getSlotStack(slot);
        }
        if (storage != nullptr) {
            return storage->getSlotStack(slot);
        }
        return machine != nullptr ? machine->getSlotStack(slot) : ItemStack{};
    };
    const auto setSlot = [&](const net::ContainerSlotSpace space, const int slot, const ItemStack& stack) {
        if (space == net::ContainerSlotSpace::Player) {
            inventoryData->inventory.setSlotStack(slot, stack);
        } else if (storage != nullptr) {
            storage->setSlotStack(slot, stack);
        } else if (machine != nullptr) {
            machine->setSlotStack(slot, stack);
        }
    };
    const auto acceptsItem = [&](const net::ContainerSlotSpace space, const int slot, const ItemID itemId) {
        if (space == net::ContainerSlotSpace::Player) {
            return true;
        }
        if (storage != nullptr) {
            return true;
        }
        return smeltingSystem != nullptr && smeltingRuntime.acceptsItem(slot, itemId, *smeltingSystem);
    };
    const auto addToSlot = [&](const net::ContainerSlotSpace space,
                               const int slot,
                               const ItemID itemId,
                               const uint32_t count) -> uint32_t {
        if (!validSlot(space, slot) || !acceptsItem(space, slot, itemId)) {
            return count;
        }
        ItemStack target = getSlot(space, slot);
        const uint32_t remaining = addToStack(target, itemId, count);
        if (remaining != count) {
            setSlot(space, slot, target);
        }
        return remaining;
    };

    ItemStack cursor = stackFromSlotData(client.openContainerCursor);
    if (action.action == net::ContainerSlotActionType::PrimaryClick) {
        if (cursor.isEmpty()) {
            const ItemStack picked = getSlot(action.slotSpace, action.slot);
            if (!picked.isEmpty()) {
                setSlot(action.slotSpace, action.slot, {});
                cursor = picked;
            }
        } else {
            const ItemID cursorItem = cursor.itemId;
            const uint32_t remaining = addToSlot(action.slotSpace, action.slot, cursorItem, cursor.count);
            if (remaining == 0) {
                cursor = {};
            } else {
                const ItemStack target = getSlot(action.slotSpace, action.slot);
                if (target.itemId == cursorItem) {
                    cursor.count = static_cast<uint16_t>(remaining);
                } else if (acceptsItem(action.slotSpace, action.slot, cursorItem)) {
                    ItemStack incoming = cursor;
                    setSlot(action.slotSpace, action.slot, incoming);
                    cursor = target;
                }
            }
        }
    } else if (action.action == net::ContainerSlotActionType::SecondaryPlace) {
        if (!cursor.isEmpty()) {
            const uint32_t remaining = addToSlot(action.slotSpace, action.slot, cursor.itemId, 1);
            if (remaining == 0) {
                --cursor.count;
                if (cursor.count == 0) {
                    cursor = {};
                }
            }
        }
    }

    client.openContainerCursor = slotDataFromStack(cursor);
    client.hasLastContainerSnapshot = false;
    client.hasLastInventorySnapshot = false;
}

void GameServer::handleClientContainerClose(ConnectedClient& client, const net::ClientContainerClose& close) {
    if (client.openContainerId == 0 || close.containerId != client.openContainerId) {
        return;
    }

    ItemStack cursor = stackFromSlotData(client.openContainerCursor);
    if (!cursor.isEmpty() && m_ecsRegistry != nullptr) {
        const entt::entity playerEntity = resolvePlayerEntity(client);
        if (playerEntity != entt::null && m_ecsRegistry->valid(playerEntity)) {
            if (auto* inventoryData = m_ecsRegistry->try_get<ecs::InventoryDataComponent>(playerEntity)) {
                uint32_t remaining = inventoryData->inventory.addItem(cursor.itemId, cursor.count);
                if (remaining > 0) {
                    const ui::ContainerUiDef& uiDef = ui::ContainerUiRegistry::require(client.openContainerUiId);
                    const ContainerBehaviorDef& behavior = ContainerBehaviorRegistry::require(uiDef.behavior);
                    if (behavior.handler == "storage" &&
                        m_ecsRegistry->ctx().contains<BlockEntityInventoryStore>()) {
                        BlockEntityInventoryStore& store = m_ecsRegistry->ctx().get<BlockEntityInventoryStore>();
                        if (BlockEntityInventory* storage = store.findMutable(client.openContainerPosition)) {
                            remaining = storage->addItem(cursor.itemId, remaining);
                        }
                    } else if (behavior.handler == "smelting" &&
                               m_ecsRegistry->ctx().contains<MachineInventoryStore>() &&
                               m_ecsRegistry->ctx().contains<SmeltingSystem>()) {
                        MachineInventoryStore& store = m_ecsRegistry->ctx().get<MachineInventoryStore>();
                        MachineInventory* machine = store.findMutable(client.openContainerPosition);
                        if (machine != nullptr) {
                            SmeltingProcessorRuntime runtime =
                                SmeltingProcessorRuntime::create(uiDef, behavior, machine->slotCount());
                            SmeltingSystem& smelting = m_ecsRegistry->ctx().get<SmeltingSystem>();
                            for (int slot = 0; slot < machine->slotCount() && remaining > 0; ++slot) {
                                if (!runtime.acceptsItem(slot, cursor.itemId, smelting)) {
                                    continue;
                                }
                                ItemStack target = machine->getSlotStack(slot);
                                const uint32_t before = remaining;
                                remaining = addToStack(target, cursor.itemId, remaining);
                                if (remaining != before) {
                                    machine->setSlotStack(slot, target);
                                }
                            }
                        }
                    }
                }
                if (remaining > 0 && m_gameplayRegistry != nullptr) {
                    ecs::ItemSpawnSystem::spawnAtPosition(*m_gameplayRegistry, cursor.itemId, client.lastPosition, remaining);
                }
            }
        }
    }

    sendContainerClose(client);
    clearOpenContainerSession(client);
    client.hasLastInventorySnapshot = false;
}

void GameServer::closeOpenContainersAtPositions(const std::vector<glm::ivec3>& positions) {
    if (positions.empty()) {
        return;
    }

    for (auto& client : m_clients) {
        if (client.openContainerId == 0) {
            continue;
        }
        if (std::find(positions.begin(), positions.end(), client.openContainerPosition) == positions.end()) {
            continue;
        }
        handleClientContainerClose(client, net::ClientContainerClose{client.openContainerId});
    }
}

void GameServer::handleClientBlockAction(ConnectedClient& client, const net::ClientBlockAction& action) {
    const glm::ivec3 actionBlock =
        (action.action == net::ClientBlockActionType::Place ||
         action.action == net::ClientBlockActionType::BucketPlaceWater)
        ? action.placeBlock
        : action.targetBlock;
    if (!ItemUseDispatcher::isWithinReach(action.playerPosition, actionBlock)) {
        return;
    }

    client.lastPosition = action.playerPosition;

    if (action.action == net::ClientBlockActionType::Interact) {
        if (!game::interaction::applyBlockInteraction(m_world, action.targetBlock)) {
            return;
        }
        MECRAFT_LOG_PRINTF("[Server] ClientBlockAction interact client=%u block=(%d,%d,%d)\n",
                           client.id,
                           action.targetBlock.x,
                           action.targetBlock.y,
                           action.targetBlock.z);
        MECRAFT_LOG_FLUSH(stdout);
        return;
    }

    if (action.action == net::ClientBlockActionType::Break) {
        const BlockID target = m_world.getBlock(action.targetBlock.x, action.targetBlock.y, action.targetBlock.z);
        if (target == RUNTIME_ID_NULL || !BlockRegistry::get(target).isSelectable) {
            return;
        }
        std::vector<glm::ivec3> removedPositions;
        const BlockID brokenBlock = removeServerTargetBlock(m_world, action.targetBlock, removedPositions);
        closeOpenContainersAtPositions(removedPositions);
        const bool dropBrokenBlockItem = client.gameplayMode != net::NetworkGameplayMode::Creative;
        constexpr bool dropContainerContents = true;
        bool brokeStorage = false;
        bool brokeMachine = false;
        const bool brokeBed = BedBlockLogic::isBedBlock(brokenBlock);
        const bool brokeDoor = DoorBlockLogic::isDoorBlock(brokenBlock);
        if (m_gameplayRegistry != nullptr) {
            brokeStorage = handleBlockEntityInventoryBreak(
                *m_gameplayRegistry,
                brokenBlock,
                action.targetBlock,
                dropContainerContents);
            brokeMachine = handleMachineInventoryBreak(
                *m_gameplayRegistry,
                brokenBlock,
                action.targetBlock,
                dropContainerContents);
            if (brokeStorage && dropBrokenBlockItem) {
                const ItemID storageItem = BlockDropTable::getDropItem(brokenBlock);
                if (storageItem != RUNTIME_ID_NULL) {
                    ecs::ItemSpawnSystem::spawn(*m_gameplayRegistry, storageItem, action.targetBlock, 1);
                }
            }
            if (brokeMachine && dropBrokenBlockItem) {
                const ItemID machineItem = BlockDropTable::getDropItem(brokenBlock);
                if (machineItem != RUNTIME_ID_NULL) {
                    ecs::ItemSpawnSystem::spawn(*m_gameplayRegistry, machineItem, action.targetBlock, 1);
                }
            }
            if (brokeBed && dropBrokenBlockItem) {
                const ItemID bedItem = BlockDropTable::getDropItem(brokenBlock);
                ecs::ItemSpawnSystem::spawn(*m_gameplayRegistry, bedItem, action.targetBlock, 1);
            }
            if (brokeDoor && dropBrokenBlockItem) {
                const ItemID doorItem = BlockDropTable::getDropItem(brokenBlock);
                ecs::ItemSpawnSystem::spawn(*m_gameplayRegistry, doorItem, action.targetBlock, 1);
            }
        } else if (m_ecsRegistry != nullptr) {
            if (m_ecsRegistry->ctx().contains<BlockEntityInventoryStore>()) {
                const auto discardedContents =
                    m_ecsRegistry->ctx().get<BlockEntityInventoryStore>().extractAndErase(action.targetBlock);
                static_cast<void>(discardedContents);
            }
            if (m_ecsRegistry->ctx().contains<MachineInventoryStore>()) {
                const auto discardedContents =
                    m_ecsRegistry->ctx().get<MachineInventoryStore>().extractAndErase(action.targetBlock);
                static_cast<void>(discardedContents);
            }
        }
        MECRAFT_LOG_PRINTF("[Server] ClientBlockAction break client=%u block=(%d,%d,%d)\n",
                           client.id,
                           action.targetBlock.x,
                           action.targetBlock.y,
                           action.targetBlock.z);
        MECRAFT_LOG_FLUSH(stdout);
        return;
    }

    if (action.action == net::ClientBlockActionType::Till) {
        if (m_ecsRegistry == nullptr || !m_ecsRegistry->valid(client.ecsPlayerEntity)) {
            return;
        }
        auto* inventoryData = m_ecsRegistry->try_get<ecs::InventoryDataComponent>(client.ecsPlayerEntity);
        auto* inventoryState = m_ecsRegistry->try_get<ecs::InventoryComponent>(client.ecsPlayerEntity);
        if (inventoryData == nullptr || inventoryState == nullptr) {
            return;
        }

        inventoryState->selectedHotbarSlot = std::clamp(static_cast<int>(client.selectedHotbarSlot),
                                                        0,
                                                        Inventory::HOTBAR_SIZE - 1);
        inventoryData->inventory.setSelectedSlot(inventoryState->selectedHotbarSlot);

        const ItemDef& selectedItemDef = ItemRegistry::get(inventoryData->inventory.getSelectedItem());
        const ItemUseRule* tillRule = ItemUseRules::findRule(selectedItemDef, ItemUseBehavior::TillSoil);
        if (tillRule == nullptr) {
            return;
        }

        if (!ItemUseDispatcher::canApplyBlockRule(m_world, action.targetBlock, *tillRule)) {
            return;
        }

        m_world.setBlockState(action.targetBlock.x,
                              action.targetBlock.y,
                              action.targetBlock.z,
                              BlockStateRegistry::getDefaultState(tillRule->resultBlock));
        MECRAFT_LOG_PRINTF("[Server] ClientBlockAction till client=%u block=(%d,%d,%d)\n",
                           client.id,
                           action.targetBlock.x,
                           action.targetBlock.y,
                           action.targetBlock.z);
        MECRAFT_LOG_FLUSH(stdout);
        return;
    }

    if (action.action == net::ClientBlockActionType::BucketPickupWater ||
        action.action == net::ClientBlockActionType::BucketPlaceWater) {
        if (m_ecsRegistry == nullptr || !m_ecsRegistry->valid(client.ecsPlayerEntity)) {
            return;
        }
        auto* inventoryData = m_ecsRegistry->try_get<ecs::InventoryDataComponent>(client.ecsPlayerEntity);
        auto* inventoryState = m_ecsRegistry->try_get<ecs::InventoryComponent>(client.ecsPlayerEntity);
        if (inventoryData == nullptr || inventoryState == nullptr) {
            return;
        }

        inventoryState->selectedHotbarSlot = std::clamp(static_cast<int>(client.selectedHotbarSlot),
                                                        0,
                                                        Inventory::HOTBAR_SIZE - 1);
        inventoryData->inventory.setSelectedSlot(inventoryState->selectedHotbarSlot);

        const ItemDef& selectedItemDef = ItemRegistry::get(inventoryData->inventory.getSelectedItem());
        if (action.action == net::ClientBlockActionType::BucketPickupWater) {
            const ItemUseRule* pickupRule =
                ItemUseRules::findRule(selectedItemDef, ItemUseBehavior::BucketPickupFluid);
            if (pickupRule == nullptr ||
                !ItemUseDispatcher::canPickupFluid(m_world, action.targetBlock, *pickupRule)) {
                return;
            }

            m_world.setFluidState(action.targetBlock.x,
                                  action.targetBlock.y,
                                  action.targetBlock.z,
                                  RUNTIME_ID_NULL);
            if (client.gameplayMode != net::NetworkGameplayMode::Creative) {
                replaceSelectedServerItem(*inventoryData, pickupRule->resultItem);
            }
            MECRAFT_LOG_PRINTF("[Server] ClientBlockAction bucket_pickup_water client=%u block=(%d,%d,%d)\n",
                               client.id,
                               action.targetBlock.x,
                               action.targetBlock.y,
                               action.targetBlock.z);
            MECRAFT_LOG_FLUSH(stdout);
            return;
        }

        const ItemUseRule* placeRule =
            ItemUseRules::findRule(selectedItemDef, ItemUseBehavior::BucketPlaceFluid);
        const StateID sourceFluid = placeRule != nullptr
            ? ItemUseDispatcher::makeSourceFluidState(placeRule->resultBlock)
            : RUNTIME_ID_NULL;
        if (placeRule == nullptr ||
            action.blockState != sourceFluid ||
            !ItemUseDispatcher::canPlaceFluid(m_world, action.placeBlock, *placeRule)) {
            return;
        }

        m_world.setFluidState(action.placeBlock.x,
                              action.placeBlock.y,
                              action.placeBlock.z,
                              sourceFluid);
        if (client.gameplayMode != net::NetworkGameplayMode::Creative) {
            replaceSelectedServerItem(*inventoryData, placeRule->resultItem);
        }
        MECRAFT_LOG_PRINTF("[Server] ClientBlockAction bucket_place_water client=%u block=(%d,%d,%d)\n",
                           client.id,
                           action.placeBlock.x,
                           action.placeBlock.y,
                           action.placeBlock.z);
        MECRAFT_LOG_FLUSH(stdout);
        return;
    }

    if (action.blockState == RUNTIME_ID_NULL) {
        return;
    }
    if (BedBlockLogic::isBedState(action.blockState)) {
        const BedBlockLogic::BedPlacement bedPlacement =
            BedBlockLogic::resolvePlacement(m_world, action.placeBlock, action.blockState);
        if (!bedPlacement.valid) {
            return;
        }

        BedBlockLogic::placeBed(m_world, bedPlacement);
        MECRAFT_LOG_PRINTF("[Server] ClientBlockAction place bed client=%u foot=(%d,%d,%d) head=(%d,%d,%d)\n",
                           client.id,
                           bedPlacement.footPos.x,
                           bedPlacement.footPos.y,
                           bedPlacement.footPos.z,
                           bedPlacement.headPos.x,
                           bedPlacement.headPos.y,
                           bedPlacement.headPos.z);
        MECRAFT_LOG_FLUSH(stdout);
        return;
    }
    if (DoorBlockLogic::isDoorState(action.blockState)) {
        const DoorBlockLogic::DoorPlacement doorPlacement =
            DoorBlockLogic::resolvePlacement(m_world, action.placeBlock, action.blockState);
        if (!doorPlacement.valid) {
            return;
        }

        DoorBlockLogic::placeDoor(m_world, doorPlacement);
        MECRAFT_LOG_PRINTF("[Server] ClientBlockAction place door client=%u lower=(%d,%d,%d) upper=(%d,%d,%d)\n",
                           client.id,
                           doorPlacement.lowerPos.x,
                           doorPlacement.lowerPos.y,
                           doorPlacement.lowerPos.z,
                           doorPlacement.upperPos.x,
                           doorPlacement.upperPos.y,
                           doorPlacement.upperPos.z);
        MECRAFT_LOG_FLUSH(stdout);
        return;
    }
    const StateID existingPlaceState =
        m_world.getBlockState(action.placeBlock.x, action.placeBlock.y, action.placeBlock.z);
    if (existingPlaceState != RUNTIME_ID_NULL &&
        !FluidState::isWater(existingPlaceState) &&
        !canReplaceWithMergedPlacementResult(existingPlaceState, action.blockState)) {
        return;
    }

    m_world.setBlock(action.placeBlock.x,
                     action.placeBlock.y,
                     action.placeBlock.z,
                     static_cast<BlockID>(action.blockState));
    if (m_gameplayRegistry != nullptr) {
        static_cast<void>(ensureBlockEntityInventoryForPlacedBlock(
            *m_gameplayRegistry,
            BlockStateRegistry::getBlockId(action.blockState),
            action.placeBlock));
    }
    MECRAFT_LOG_PRINTF("[Server] ClientBlockAction place client=%u block=(%d,%d,%d) state=%u\n",
                       client.id,
                       action.placeBlock.x,
                       action.placeBlock.y,
                       action.placeBlock.z,
                       static_cast<unsigned>(action.blockState));
    MECRAFT_LOG_FLUSH(stdout);
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
        snapshot.playerPoseCorrected = client.playerPoseCorrectionPending;

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

void GameServer::sendContainerClose(ConnectedClient& client) {
    if (client.openContainerId == 0 ||
        !client.receivedHello ||
        !client.transport ||
        !client.transport->hasActiveRemote()) {
        return;
    }

    net::Packet packet;
    packet.channel = net::PacketChannel::ReliableWorld;
    packet.type = net::MessageType::ContainerClose;
    net::ContainerCloseMessage close;
    close.containerId = client.openContainerId;
    close.blockPosition = client.openContainerPosition;
    packet.inProcessPayload = close;
    client.transport->send(std::move(packet));
}

void GameServer::sendContainerSnapshotsToClients() {
    for (auto& client : m_clients) {
        if (!client.receivedHello ||
            !client.transport ||
            !client.transport->hasActiveRemote() ||
            client.openContainerId == 0) {
            continue;
        }

        net::ContainerSnapshotMessage snapshot;
        if (!buildContainerSnapshot(client, snapshot)) {
            handleClientContainerClose(client, net::ClientContainerClose{client.openContainerId});
            continue;
        }

        const bool changed = !client.hasLastContainerSnapshot ||
                             !containerSnapshotsEqual(client.lastContainerSnapshot, snapshot);
        if (!changed) {
            continue;
        }

        net::Packet packet;
        packet.channel = net::PacketChannel::ReliableWorld;
        packet.type = net::MessageType::ContainerSnapshot;
        packet.inProcessPayload = snapshot;
        client.transport->send(std::move(packet));

        client.hasLastContainerSnapshot = true;
        client.lastContainerSnapshot = std::move(snapshot);
    }
}

void GameServer::sendBlockUpdatesToClients() {
    if (m_pendingBlockUpdates.empty()) {
        return;
    }

    for (auto& client : m_clients) {
        if (!client.receivedHello || !client.transport || !client.transport->hasActiveRemote()) {
            continue;
        }

        net::BlockUpdateBatchMessage batch;
        batch.updates.reserve(m_pendingBlockUpdates.size());
        for (const net::BlockUpdateEntry& update : m_pendingBlockUpdates) {
            if (client.sentChunks.find(blockUpdateChunkKey(update)) == client.sentChunks.end()) {
                continue;
            }
            batch.updates.push_back(update);
        }

        if (batch.updates.empty()) {
            continue;
        }

        net::Packet packet;
        packet.channel = net::PacketChannel::ReliableWorld;
        packet.type = net::MessageType::BlockUpdateBatch;
        packet.inProcessPayload = std::move(batch);
        client.transport->send(std::move(packet));
    }

    m_pendingBlockUpdates.clear();
}

net::BlockUpdateEntry GameServer::makeBlockOnlyUpdateEntry(const int x, const int y, const int z, const StateID stateId) const {
    net::BlockUpdateEntry entry;
    entry.x = x;
    entry.y = y;
    entry.z = z;
    entry.stateId = stateId;
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
    entry.stateId = kLightOnlyBlockUpdate;
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
                                                       const StateID stateId,
                                                       const int lightPatchRadius) const {
    net::BlockUpdateEntry entry;
    entry.x = x;
    entry.y = y;
    entry.z = z;
    entry.stateId = stateId;
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
                MECRAFT_LOG_PRINTF("[Server] Sent PlayerSpawn receiver=%u sourceClient=%u netId=%u\n",
                                   receiver.id,
                                   other.id,
                                   other.playerNetId);
                MECRAFT_LOG_FLUSH(stdout);
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
        if (const auto* entityType = reg.try_get<ecs::EntityTypeComponent>(entity);
            entityType != nullptr && !entityType->entityId.empty()) {
            data.type = entityType->entityId;
        } else {
            data.type = "minecraft:zombie";
        }
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

    auto dropView = reg.view<ecs::DropItemTag,
                             ecs::DropEntityIdComponent,
                             ecs::TransformComponent,
                             ecs::ItemComponent,
                             ecs::VelocityComponent,
                             ecs::BoundsComponent,
                             ecs::LifetimeComponent,
                             ecs::SpinVisualComponent,
                             ecs::GroundedStateComponent>();
    for (const entt::entity entity : dropView) {
        const auto& item = dropView.get<ecs::ItemComponent>(entity);
        if (item.itemId == 0 || item.stackCount == 0) {
            continue;
        }

        save::PersistentEntityData data;
        data.type = "minecraft:item";
        const auto& id = dropView.get<ecs::DropEntityIdComponent>(entity);
        const auto& transform = dropView.get<ecs::TransformComponent>(entity);
        const auto& velocity = dropView.get<ecs::VelocityComponent>(entity);
        const auto& bounds = dropView.get<ecs::BoundsComponent>(entity);
        const auto& lifetime = dropView.get<ecs::LifetimeComponent>(entity);
        const auto& spin = dropView.get<ecs::SpinVisualComponent>(entity);
        const auto& grounded = dropView.get<ecs::GroundedStateComponent>(entity);

        data.dropId = static_cast<uint64_t>(id.dropId);
        data.itemId = static_cast<uint32_t>(item.itemId);
        data.stackCount = item.stackCount;
        data.posX = transform.position.x;
        data.posY = transform.position.y;
        data.posZ = transform.position.z;
        data.velX = velocity.velocity.x;
        data.velY = velocity.velocity.y;
        data.velZ = velocity.velocity.z;
        data.halfExtentX = bounds.halfExtents.x;
        data.halfExtentY = bounds.halfExtents.y;
        data.halfExtentZ = bounds.halfExtents.z;
        data.yaw = spin.yawRadians;
        data.spinSpeed = spin.spinSpeedRadians;
        data.ageSeconds = lifetime.ageSeconds;
        data.lifeTimeSeconds = lifetime.lifeTimeSeconds;
        data.grounded = grounded.grounded;
        entities.push_back(data);
    }

    return entities;
}

std::vector<save::BlockEntityData> GameServer::snapshotBlockEntities() const {
    std::vector<save::BlockEntityData> entities;
    std::unordered_map<BlockEntityPositionKey, std::size_t, BlockEntityPositionHash> entityByPosition;

    const auto ensureBlockEntityEntry =
        [&entities, &entityByPosition](const glm::ivec3& position,
                                       const std::string& type) -> save::BlockEntityData& {
        const BlockEntityPositionKey key{position.x, position.y, position.z};
        const auto found = entityByPosition.find(key);
        if (found != entityByPosition.end()) {
            return entities[found->second];
        }

        save::BlockEntityData data;
        data.type = type;
        data.x = position.x;
        data.y = position.y;
        data.z = position.z;
        entities.push_back(std::move(data));
        const std::size_t index = entities.size() - 1u;
        entityByPosition.emplace(key, index);
        return entities[index];
    };

    for (const auto& [chunkKey, chunk] : m_world.getActiveChunks()) {
        static_cast<void>(chunkKey);
        if (!chunk) {
            continue;
        }

        const glm::ivec3 worldOffset = chunk->getWorldOffset();
        for (int scy = 0; scy < Chunk::NUM_SUB_CHUNKS; ++scy) {
            const SubChunk* subChunk = chunk->getSubChunk(scy);
            if (subChunk == nullptr || subChunk->getType() == SubChunkType::Air) {
                continue;
            }

            const int yBase = scy * SubChunk::SIZE;
            for (int ly = 0; ly < SubChunk::SIZE; ++ly) {
                for (int z = 0; z < Chunk::SIZE_Z; ++z) {
                    for (int x = 0; x < Chunk::SIZE_X; ++x) {
                        const StateID state = subChunk->getBlock(x, ly, z);
                        const BlockID blockId = BlockStateRegistry::getBlockId(state);
                        std::string blockEntityType;
                        if (persistentBlockEntityTypeForBlock(blockId, blockEntityType)) {
                            ensureBlockEntityEntry(glm::ivec3(worldOffset.x + x,
                                                              yBase + ly,
                                                              worldOffset.z + z),
                                                   blockEntityType);
                        }
                    }
                }
            }
        }
    }

    if (m_ecsRegistry == nullptr) {
        return entities;
    }

    if (m_ecsRegistry->ctx().contains<BlockEntityInventoryStore>()) {
        const BlockEntityInventoryStore& store = m_ecsRegistry->ctx().get<BlockEntityInventoryStore>();
        store.forEach([&ensureBlockEntityEntry](const glm::ivec3& position,
                                                 const std::string& typeId,
                                                 const int slotCount,
                                                 const BlockEntityInventory& inventory) {
            save::BlockEntityData& data = ensureBlockEntityEntry(position, typeId);
            data.type = typeId;
            data.slots.clear();

            for (int slot = 0; slot < slotCount; ++slot) {
                const ItemStack stack = inventory.getSlotStack(slot);
                if (stack.isEmpty()) {
                    continue;
                }

                save::BlockEntitySlotData slotData;
                slotData.slot = slot;
                slotData.itemId = static_cast<uint32_t>(stack.itemId);
                slotData.count = stack.count;
                slotData.durability = stack.durability;
                data.slots.push_back(slotData);
            }
        });
    }

    if (m_ecsRegistry->ctx().contains<MachineInventoryStore>()) {
        const MachineInventoryStore& store = m_ecsRegistry->ctx().get<MachineInventoryStore>();
        store.forEach([&ensureBlockEntityEntry](const glm::ivec3& position,
                                                 const std::string& typeId,
                                                 const MachineInventory& machine) {
            save::BlockEntityData& data = ensureBlockEntityEntry(position, typeId);
            data.type = typeId;
            data.slots.clear();
            data.burnSecondsRemaining = machine.burnSecondsRemaining();
            data.burnSecondsTotal = machine.burnSecondsTotal();
            data.cookSeconds = machine.cookSeconds();
            data.cookTargetSeconds = machine.cookTargetSeconds();

            for (int slot = 0; slot < machine.slotCount(); ++slot) {
                const ItemStack stack = machine.getSlotStack(slot);
                if (stack.isEmpty()) {
                    continue;
                }

                save::BlockEntitySlotData slotData;
                slotData.slot = slot;
                slotData.itemId = static_cast<uint32_t>(stack.itemId);
                slotData.count = stack.count;
                slotData.durability = stack.durability;
                data.slots.push_back(slotData);
            }
        });
    }

    return entities;
}

void GameServer::savePersistentEntities() {
    if (!m_saveManager || m_ecsRegistry == nullptr) {
        return;
    }
    m_saveManager->savePersistentEntities(snapshotPersistentEntities());
}

void GameServer::saveBlockEntities() {
    if (!m_saveManager) {
        return;
    }
    std::vector<save::BlockEntityData> entities = snapshotBlockEntities();
    if (m_ecsRegistry == nullptr && entities.empty()) {
        return;
    }
    m_saveManager->saveBlockEntities(entities);
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
        if (data.type == "minecraft:item") {
            if (data.itemId == 0 || data.stackCount == 0) {
                continue;
            }

            ecs::ItemDropSpawnParams params;
            params.itemId = static_cast<ItemID>(data.itemId);
            params.stackCount = data.stackCount;
            params.position = glm::vec3(data.posX, data.posY, data.posZ);
            params.velocity = glm::vec3(data.velX, data.velY, data.velZ);
            params.halfExtents = glm::vec3(data.halfExtentX, data.halfExtentY, data.halfExtentZ);
            params.yawRadians = data.yaw;
            params.spinSpeedRadians = data.spinSpeed;
            params.ageSeconds = data.ageSeconds;
            params.lifeTimeSeconds = data.lifeTimeSeconds;
            params.grounded = data.grounded;
            params.dropId = static_cast<std::size_t>(data.dropId);
            ecs::EntityFactory::createItemDrop(*m_gameplayRegistry, params);
            continue;
        }

        if (data.health <= 0) {
            continue;
        }

        const glm::vec3 position(data.posX, data.posY, data.posZ);
        const entt::entity mob = ecs::EntityFactory::createMob(*m_gameplayRegistry, data.type, position);
        if (mob == entt::null) {
            continue;
        }
        entt::registry& reg = m_gameplayRegistry->registry();

        if (auto* health = reg.try_get<ecs::HealthComponent>(mob)) {
            health->current = data.health;
            health->max = data.healthMax > 0 ? data.healthMax : health->max;
        }
        if (auto* ai = reg.try_get<ecs::MobAIComponent>(mob)) {
            ai->yaw = data.yaw;
        }
        if (auto* physicsBody = reg.try_get<ecs::PhysicsBodyComponent>(mob)) {
            physicsBody->body.velocity = glm::vec3(data.velX, data.velY, data.velZ);
        }
    }
}

void GameServer::restoreBlockEntities() {
    if (!m_saveManager || m_ecsRegistry == nullptr) {
        return;
    }
    if (!m_blockEntitiesRestorePending) {
        return;
    }
    m_blockEntitiesRestorePending = false;

    std::vector<save::BlockEntityData> entities;
    if (!m_saveManager->loadBlockEntities(entities)) {
        return;
    }
    if (entities.empty()) {
        return;
    }

    for (const save::BlockEntityData& data : entities) {
        const ContainerBehaviorDef& behavior = ContainerBehaviorRegistry::require(data.type);
        if (behavior.storage.kind != ContainerStorageKind::BlockEntity) {
            throw std::runtime_error(behavior.id + " block entity restore requires block_entity storage");
        }

        if (behavior.handler == "smelting") {
            MachineInventoryStore& store = m_ecsRegistry->ctx().contains<MachineInventoryStore>()
                ? m_ecsRegistry->ctx().get<MachineInventoryStore>()
                : m_ecsRegistry->ctx().emplace<MachineInventoryStore>();
            MachineInventory& machine = store.getOrCreate(
                glm::ivec3(data.x, data.y, data.z),
                behavior.id,
                behavior.storage.slots);
            machine.setProgress(data.burnSecondsRemaining,
                                data.burnSecondsTotal,
                                data.cookSeconds,
                                data.cookTargetSeconds);
            for (const save::BlockEntitySlotData& slot : data.slots) {
                if (!machine.isValidSlot(slot.slot) ||
                    slot.slot >= behavior.storage.slots ||
                    slot.itemId == 0 ||
                    slot.count == 0) {
                    continue;
                }

                ItemStack stack;
                stack.itemId = static_cast<ItemID>(slot.itemId);
                stack.count = static_cast<uint16_t>(std::min<uint32_t>(slot.count, 65535u));
                stack.durability = static_cast<uint16_t>(std::min<uint32_t>(slot.durability, 65535u));
                machine.setSlotStack(slot.slot, stack);
            }
            continue;
        }

        if (behavior.handler != "storage") {
            continue;
        }

        BlockEntityInventoryStore& store = m_ecsRegistry->ctx().contains<BlockEntityInventoryStore>()
            ? m_ecsRegistry->ctx().get<BlockEntityInventoryStore>()
            : m_ecsRegistry->ctx().emplace<BlockEntityInventoryStore>();
        BlockEntityInventory& inventory = store.getOrCreate(
            glm::ivec3(data.x, data.y, data.z),
            behavior.id,
            behavior.storage.slots);
        for (const save::BlockEntitySlotData& slot : data.slots) {
            if (!inventory.isValidSlot(slot.slot) ||
                slot.slot >= behavior.storage.slots ||
                slot.itemId == 0 ||
                slot.count == 0) {
                continue;
            }

            ItemStack stack;
            stack.itemId = static_cast<ItemID>(slot.itemId);
            stack.count = static_cast<uint16_t>(std::min<uint32_t>(slot.count, 65535u));
            stack.durability = static_cast<uint16_t>(std::min<uint32_t>(slot.durability, 65535u));
            inventory.setSlotStack(slot.slot, stack);
        }
    }
}

bool GameServer::spawnMobEntity(const std::string& entityId, const glm::vec3& position) {
    if (m_gameplayRegistry != nullptr) {
        return ecs::EntityFactory::createMob(*m_gameplayRegistry, entityId, position) != entt::null;
    }

    if (m_ecsRegistry == nullptr) {
        return false;
    }

    if (entityId == "minecraft:zombie") {
        ecs::EntityFactory::createZombie(*m_ecsRegistry, position);
        return true;
    }
    return false;
}

bool GameServer::spawnZombieEntity(const glm::vec3& position) {
    return spawnMobEntity("minecraft:zombie", position);
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
    } else if (reg.all_of<ecs::ProjectileTag, ecs::ItemComponent>(entity)) {
        msg.kind = net::EntityKind::Projectile;
        const auto& item = reg.get<ecs::ItemComponent>(entity);
        msg.itemId = static_cast<uint16_t>(item.itemId);
        msg.stackCount = static_cast<uint16_t>(std::max<uint32_t>(1u, item.stackCount));
    } else if (reg.all_of<ecs::MobTag>(entity)) {
        msg.kind = net::EntityKind::Mob;
        if (const auto* entityType = reg.try_get<ecs::EntityTypeComponent>(entity);
            entityType != nullptr && !entityType->entityId.empty()) {
            msg.entityId = entityType->entityId;
        } else {
            msg.entityId = "minecraft:zombie";
        }
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
    if (!m_ecsRegistry) {
        return;
    }

    auto& reg = *m_ecsRegistry;

    auto sendToConnectedClients = [this](const net::Packet& packet) {
        for (auto& client : m_clients) {
            if (!client.receivedHello || !client.transport || !client.transport->hasActiveRemote()) {
                continue;
            }
            net::Packet copy = packet;
            client.transport->send(std::move(copy));
        }
    };

    // 1. Detect new entities with NetworkSyncTag that don't have EntityNetId yet
    auto newSyncView = reg.view<ecs::NetworkSyncTag>(entt::exclude<ecs::EntityNetIdComponent>);
    for (auto entity : newSyncView) {
        reg.emplace<ecs::EntityNetIdComponent>(entity, m_nextNetId);
        m_syncedEntities[m_nextNetId] = entity;
        ++m_nextNetId;
    }

    // 2. Send final impact/despawn events for entities intentionally retired by ECS systems.
    std::vector<ecs::EntityNetId> pendingNetworkDespawns;
    for (const auto& [netId, entity] : m_syncedEntities) {
        if (reg.valid(entity) && reg.all_of<ecs::PendingNetworkDespawnTag>(entity)) {
            pendingNetworkDespawns.push_back(netId);
        }
    }

    for (const ecs::EntityNetId netId : pendingNetworkDespawns) {
        auto syncedIt = m_syncedEntities.find(netId);
        if (syncedIt == m_syncedEntities.end()) {
            continue;
        }

        const entt::entity entity = syncedIt->second;
        if (reg.valid(entity)) {
            if (const auto* impact = reg.try_get<ecs::EntityImpactComponent>(entity);
                impact != nullptr && impact->particleBlock != 0) {
                net::Packet impactPacket;
                impactPacket.channel = net::PacketChannel::ReliableWorld;
                impactPacket.type = net::MessageType::EntityImpact;
                net::EntityImpactMessage impactMsg;
                impactMsg.netId = netId;
                impactMsg.position = impact->position;
                impactMsg.particleBlockId = static_cast<uint16_t>(impact->particleBlock);
                impactMsg.particleCount = static_cast<uint16_t>(std::clamp(impact->particleCount, 0, 65535));
                impactPacket.inProcessPayload = impactMsg;
                sendToConnectedClients(impactPacket);
            }
        }

        for (auto& client : m_clients) {
            client.spawnedEntityNetIds.erase(netId);
        }

        net::Packet despawnPacket;
        despawnPacket.channel = net::PacketChannel::ReliableWorld;
        despawnPacket.type = net::MessageType::EntityDespawn;
        net::EntityDespawnMessage despawnMsg;
        despawnMsg.netId = netId;
        despawnPacket.inProcessPayload = despawnMsg;
        sendToConnectedClients(despawnPacket);

        if (reg.valid(entity)) {
            std::vector<entt::entity> toDestroy;
            collectEntityTree(reg, entity, toDestroy);
            for (const entt::entity doomed : toDestroy) {
                if (reg.valid(doomed)) {
                    reg.destroy(doomed);
                }
            }
        }
        m_syncedEntities.erase(syncedIt);
    }

    // 3. Detect despawned entities (entities in m_syncedEntities that are no longer valid)
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

    // 4. Send spawn messages once per connected client.
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

    // 5. Build and send entity snapshots (batch of all synced entities)
    net::EntitySnapshotMessage snapshot;
    snapshot.serverTick = m_currentTick;
    std::vector<entt::entity> hurtEventsSent;

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

        if (const auto* health = reg.try_get<ecs::HealthComponent>(entity)) {
            item.health = static_cast<uint16_t>(std::clamp(health->current, 0, 65535));
            item.maxHealth = static_cast<uint16_t>(std::clamp(health->max, 0, 65535));
        }
        if (auto* hurt = reg.try_get<ecs::HurtEffectComponent>(entity)) {
            item.hurt = hurt->classicHurtEffectPending;
            if (item.hurt) {
                hurtEventsSent.push_back(entity);
            }
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

    for (const entt::entity entity : hurtEventsSent) {
        if (!reg.valid(entity)) {
            continue;
        }
        if (auto* hurt = reg.try_get<ecs::HurtEffectComponent>(entity)) {
            hurt->classicHurtEffectPending = false;
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
        MECRAFT_LOG_PRINTF("[Server] Sent ChunkData client=%u chunk=(%d,%d) total=%d active=%zu\n",
                           client.id,
                           cx,
                           cz,
                           client.totalChunksSent,
                           m_world.getActiveChunks().size());
        MECRAFT_LOG_FLUSH(stdout);
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
