#include "GameClient.h"
#include "../Diagnostics.h"
#include "../ecs/GameplayRegistry.h"
#include "../ecs/components/Components.h"
#include "../world/chunk/Chunk.h"
#include "../world/WeatherSystem.h"
#include <algorithm>
#include <cstdio>

namespace client {

GameClient::GameClient() = default;
GameClient::~GameClient() = default;

void GameClient::connect(std::unique_ptr<net::ITransportEndpoint> transport) {
    m_transport = std::move(transport);
    m_playerHurtLatched = false;
    MECRAFT_LOG_PRINTF("[Client] Transport connected; sending hello\n");
    MECRAFT_LOG_FLUSH(stdout);
    sendHello();
}

void GameClient::initEntityStore(entt::registry& registry, ResourceMgr* resourceMgr) {
    m_ecsRegistry = &registry;
    m_entityStore.init(registry, resourceMgr);
}

void GameClient::initEntityStore(ecs::GameplayRegistry& registry, ResourceMgr* resourceMgr) {
    m_ecsRegistry = &registry.registry();
    m_entityStore.init(registry, resourceMgr);
}

void GameClient::sendHello() {
    if (!m_transport) return;

    net::Packet packet;
    packet.channel = net::PacketChannel::ReliableControl;
    packet.type = net::MessageType::ClientHello;
    net::ClientHello hello;
    hello.protocolVersion = 1;
    packet.inProcessPayload = hello;
    m_transport->send(std::move(packet));
}

void GameClient::sendViewConfig(int renderDistance) {
    if (!m_transport) return;

    net::Packet packet;
    packet.channel = net::PacketChannel::ReliableControl;
    packet.type = net::MessageType::ClientViewConfig;
    net::ClientViewConfig config;
    config.renderDistance = renderDistance;
    packet.inProcessPayload = config;
    m_transport->send(std::move(packet));
}

void GameClient::sendInput(float dt, const glm::vec3& moveInput,
                           const glm::vec2& lookDelta,
                           bool jump, bool sneak, bool sprint) {
    sendInput(dt,
              moveInput,
              lookDelta,
              jump,
              sneak,
              sprint,
              glm::vec3(0.0f),
              glm::vec3(0.0f),
              0.0f,
              0.0f);
}

void GameClient::sendInput(float dt, const glm::vec3& moveInput,
                           const glm::vec2& lookDelta,
                           bool jump, bool sneak, bool sprint,
                           const glm::vec3& playerPosition,
                           const glm::vec3& playerVelocity,
                           float yaw,
                           float pitch,
                           const uint32_t actions,
                           const uint8_t selectedHotbarSlot) {
    if (!m_transport) return;

    ++m_inputSequence;

    net::Packet packet;
    packet.channel = net::PacketChannel::UnreliableState;
    packet.type = net::MessageType::ClientInput;
    net::ClientInput input;
    input.sequence = m_inputSequence;
    input.dt = dt;
    input.moveInput = moveInput;
    input.lookDelta = lookDelta;
    input.playerPosition = playerPosition;
    input.playerVelocity = playerVelocity;
    input.yaw = yaw;
    input.pitch = pitch;
    input.jump = jump;
    input.sneak = sneak;
    input.sprint = sprint;
    input.actions = actions;
    input.selectedHotbarSlot = selectedHotbarSlot;
    packet.inProcessPayload = input;
    m_transport->send(std::move(packet));
}

void GameClient::sendBlockAction(const net::ClientBlockAction& action) {
    if (!m_transport) return;

    net::Packet packet;
    packet.channel = net::PacketChannel::ReliableWorld;
    packet.type = net::MessageType::ClientBlockAction;
    packet.inProcessPayload = action;
    m_transport->send(std::move(packet));
}

void GameClient::sendContainerOpenRequest(const glm::ivec3& blockPosition, const glm::vec3& playerPosition) {
    if (!m_transport) return;

    net::Packet packet;
    packet.channel = net::PacketChannel::ReliableWorld;
    packet.type = net::MessageType::ClientContainerOpenRequest;
    net::ClientContainerOpenRequest request;
    request.sequence = ++m_containerSequence;
    request.blockPosition = blockPosition;
    request.playerPosition = playerPosition;
    packet.inProcessPayload = request;
    m_transport->send(std::move(packet));
}

void GameClient::sendContainerSlotAction(const net::ClientContainerSlotAction& action) {
    if (!m_transport) return;

    net::Packet packet;
    packet.channel = net::PacketChannel::ReliableWorld;
    packet.type = net::MessageType::ClientContainerSlotAction;
    net::ClientContainerSlotAction outgoing = action;
    outgoing.sequence = ++m_containerSequence;
    packet.inProcessPayload = outgoing;
    m_transport->send(std::move(packet));
}

void GameClient::sendContainerClose(const uint32_t containerId) {
    if (!m_transport || containerId == 0) return;

    net::Packet packet;
    packet.channel = net::PacketChannel::ReliableWorld;
    packet.type = net::MessageType::ClientContainerClose;
    net::ClientContainerClose close;
    close.containerId = containerId;
    packet.inProcessPayload = close;
    m_transport->send(std::move(packet));
}

void GameClient::sendChatMessage(const std::string& message) {
    if (!m_transport || message.empty()) return;

    net::Packet packet;
    packet.channel = net::PacketChannel::ReliableChat;
    packet.type = net::MessageType::ClientChatMessage;
    net::ClientChatMessage chat;
    chat.message = message;
    packet.inProcessPayload = std::move(chat);
    m_transport->send(std::move(packet));
}

void GameClient::sendCommandRequest(const std::string& command) {
    if (!m_transport || command.empty()) return;

    net::Packet packet;
    packet.channel = net::PacketChannel::ReliableChat;
    packet.type = net::MessageType::ClientCommandRequest;
    net::ClientCommandRequest request;
    request.sequence = ++m_commandSequence;
    request.command = command;
    packet.inProcessPayload = std::move(request);
    m_transport->send(std::move(packet));
}

void GameClient::sendRespawnRequest() {
    if (!m_transport) return;

    net::Packet packet;
    packet.channel = net::PacketChannel::ReliableControl;
    packet.type = net::MessageType::ClientRespawnRequest;
    net::ClientRespawnRequest request;
    request.sequence = ++m_respawnSequence;
    packet.inProcessPayload = request;
    m_transport->send(std::move(packet));
}

void GameClient::receiveMessages() {
    if (!m_transport) return;

    net::Packet packet;
    while (m_transport->tryReceive(packet)) {
        switch (packet.type) {
        case net::MessageType::ServerHello: {
            if (packet.inProcessPayload.has_value()) {
                const auto& hello = std::any_cast<const net::ServerHello&>(packet.inProcessPayload);
                m_clientId = hello.assignedId;
                m_authPosition = hello.spawnPosition;
                m_hasServerHello = true;
                MECRAFT_LOG_PRINTF("[Client] ServerHello id=%u spawn=(%.1f, %.1f, %.1f)\n",
                                   m_clientId,
                                   m_authPosition.x,
                                   m_authPosition.y,
                                   m_authPosition.z);
                MECRAFT_LOG_FLUSH(stdout);
            } else {
                MECRAFT_LOG_PRINTF("[Client] Received ServerHello without decoded payload bytes=%zu\n",
                                   packet.payload.size());
                MECRAFT_LOG_FLUSH(stdout);
            }
            break;
        }
        case net::MessageType::ChunkData: {
            if (packet.inProcessPayload.has_value()) {
                const auto& data = std::any_cast<const net::ChunkDataMessage&>(packet.inProcessPayload);
                handleChunkData(data);
            }
            break;
        }
        case net::MessageType::ChunkUnload: {
            if (packet.inProcessPayload.has_value()) {
                const auto& data = std::any_cast<const net::ChunkUnloadMessage&>(packet.inProcessPayload);
                m_clientWorld.removeChunk(data.chunkX, data.chunkZ);
            }
            break;
        }
        case net::MessageType::BlockUpdateBatch: {
            if (packet.inProcessPayload.has_value()) {
                const auto& batch = std::any_cast<const net::BlockUpdateBatchMessage&>(packet.inProcessPayload);
                for (const auto& update : batch.updates) {
                    m_clientWorld.applyBlockUpdate(update.x, update.y, update.z,
                                                   static_cast<StateID>(update.stateId),
                                                   update.packedLightPatch);
                }
            }
            break;
        }
        case net::MessageType::ServerSnapshot: {
            if (packet.inProcessPayload.has_value()) {
                const auto& snapshot = std::any_cast<const net::ServerSnapshot&>(packet.inProcessPayload);
                handleServerSnapshot(snapshot);
            }
            break;
        }
        case net::MessageType::EntitySpawn: {
            if (packet.inProcessPayload.has_value()) {
                const auto& msg = std::any_cast<const net::EntitySpawnMessage&>(packet.inProcessPayload);
                m_entityStore.handleSpawn(msg);
            }
            break;
        }
        case net::MessageType::EntityDespawn: {
            if (packet.inProcessPayload.has_value()) {
                const auto& msg = std::any_cast<const net::EntityDespawnMessage&>(packet.inProcessPayload);
                m_entityStore.handleDespawn(msg);
            }
            break;
        }
        case net::MessageType::EntityImpact: {
            if (packet.inProcessPayload.has_value()) {
                const auto& msg = std::any_cast<const net::EntityImpactMessage&>(packet.inProcessPayload);
                m_entityStore.handleImpact(msg);
            }
            break;
        }
        case net::MessageType::EntitySnapshot: {
            if (packet.inProcessPayload.has_value()) {
                const auto& msg = std::any_cast<const net::EntitySnapshotMessage&>(packet.inProcessPayload);
                m_entityStore.handleSnapshot(msg);
            }
            break;
        }
        case net::MessageType::InventorySnapshot: {
            if (packet.inProcessPayload.has_value()) {
                const auto& msg = std::any_cast<const net::InventorySnapshotMessage&>(packet.inProcessPayload);
                handleInventorySnapshot(msg);
            }
            break;
        }
        case net::MessageType::ContainerSnapshot: {
            if (packet.inProcessPayload.has_value()) {
                const auto& msg = std::any_cast<const net::ContainerSnapshotMessage&>(packet.inProcessPayload);
                handleContainerSnapshot(msg);
            }
            break;
        }
        case net::MessageType::ContainerClose: {
            if (packet.inProcessPayload.has_value()) {
                const auto& msg = std::any_cast<const net::ContainerCloseMessage&>(packet.inProcessPayload);
                handleContainerClose(msg);
            }
            break;
        }
        case net::MessageType::ServerChatMessage: {
            if (packet.inProcessPayload.has_value() && m_chatMessageCallback) {
                const auto& msg = std::any_cast<const net::ServerChatMessage&>(packet.inProcessPayload);
                m_chatMessageCallback(msg);
            }
            break;
        }
        case net::MessageType::ServerSystemMessage: {
            if (packet.inProcessPayload.has_value() && m_systemMessageCallback) {
                const auto& msg = std::any_cast<const net::ServerSystemMessage&>(packet.inProcessPayload);
                m_systemMessageCallback(msg);
            }
            break;
        }
        case net::MessageType::CommandResult: {
            if (packet.inProcessPayload.has_value() && m_commandResultCallback) {
                const auto& msg = std::any_cast<const net::CommandResultMessage&>(packet.inProcessPayload);
                m_commandResultCallback(msg);
            }
            break;
        }
        case net::MessageType::WorldStateSnapshot: {
            if (packet.inProcessPayload.has_value()) {
                const auto& msg = std::any_cast<const net::WorldStateSnapshotMessage&>(packet.inProcessPayload);
                handleWorldStateSnapshot(msg);
            }
            break;
        }
        case net::MessageType::PlayerModeUpdate: {
            if (packet.inProcessPayload.has_value()) {
                const auto& msg = std::any_cast<const net::PlayerModeUpdateMessage&>(packet.inProcessPayload);
                handlePlayerModeUpdate(msg);
            }
            break;
        }
        default:
            break;
        }
    }
}

void GameClient::handleChunkData(const net::ChunkDataMessage& data) {
    if (data.chunk) {
        m_clientWorld.addChunk(data.chunk);
        ++m_chunksReceived;
        if (m_chunksReceived <= 12 || m_chunksReceived % 25 == 0) {
            MECRAFT_LOG_PRINTF("[Client] Received ChunkData chunk=(%d,%d) count=%d loaded=%zu\n",
                               data.chunkX,
                               data.chunkZ,
                               m_chunksReceived,
                               m_clientWorld.loadedChunkCount());
            MECRAFT_LOG_FLUSH(stdout);
        }

        if (!m_spawnChunksReady && m_chunksReceived >= kSpawnChunksThreshold) {
            m_spawnChunksReady = true;
            MECRAFT_LOG_PRINTF("[Client] Spawn chunks ready after %d chunks\n", m_chunksReceived);
            MECRAFT_LOG_FLUSH(stdout);
        }
    }
}

void GameClient::handleServerSnapshot(const net::ServerSnapshot& snapshot) {
    m_lastSnapshot = snapshot;
    m_authPosition = snapshot.authoritativePosition;

    if (m_ecsRegistry == nullptr) {
        return;
    }

    auto view = m_ecsRegistry->view<ecs::LocalPlayerTag, ecs::HealthComponent>();
    for (const entt::entity player : view) {
        auto& health = view.get<ecs::HealthComponent>(player);
        const bool healthDropped = static_cast<int>(snapshot.playerHealth) < health.current;
        const bool hurtFlagRising = snapshot.playerHurt && !m_playerHurtLatched;
        health.current = static_cast<int>(snapshot.playerHealth);
        health.max = static_cast<int>(snapshot.playerMaxHealth);

        if (snapshot.playerRespawned || snapshot.playerPoseCorrected) {
            if (auto* transform = m_ecsRegistry->try_get<ecs::TransformComponent>(player)) {
                transform->position = snapshot.authoritativePosition;
            }
            if (auto* velocity = m_ecsRegistry->try_get<ecs::VelocityComponent>(player)) {
                velocity->velocity = snapshot.authoritativeVelocity;
            }
            if (auto* body = m_ecsRegistry->try_get<ecs::PhysicsBodyComponent>(player)) {
                body->body.position = snapshot.authoritativePosition;
                body->body.velocity = snapshot.authoritativeVelocity;
            }
        }

        if (hurtFlagRising || healthDropped) {
            if (auto* hurt = m_ecsRegistry->try_get<ecs::HurtEffectComponent>(player)) {
                hurt->triggerClassicHurt();
            }
        }
        m_playerHurtLatched = snapshot.playerHurt;
        break;
    }
}

void GameClient::handleInventorySnapshot(const net::InventorySnapshotMessage& snapshot) {
    if (m_ecsRegistry == nullptr) {
        return;
    }

    auto view = m_ecsRegistry->view<ecs::LocalPlayerTag>();
    for (const entt::entity player : view) {
        auto* inventoryState = m_ecsRegistry->try_get<ecs::InventoryComponent>(player);
        if (inventoryState == nullptr) {
            inventoryState = &m_ecsRegistry->emplace<ecs::InventoryComponent>(player);
        }

        const int selectedSlot = std::clamp(static_cast<int>(snapshot.selectedHotbarSlot),
                                            0,
                                            Inventory::HOTBAR_SIZE - 1);
        inventoryState->selectedHotbarSlot = selectedSlot;

        auto* inventoryData = m_ecsRegistry->try_get<ecs::InventoryDataComponent>(player);
        if (inventoryData == nullptr) {
            inventoryData = &m_ecsRegistry->emplace<ecs::InventoryDataComponent>(player);
        }
        inventoryData->inventory.setSelectedSlot(selectedSlot);

        for (int slot = 0; slot < Inventory::INVENTORY_SIZE; ++slot) {
            ItemStack stack{};
            if (slot < static_cast<int>(snapshot.slots.size())) {
                const net::InventorySlotData& remote = snapshot.slots[slot];
                if (remote.itemId != 0 && remote.stackCount != 0) {
                    stack.itemId = remote.itemId;
                    stack.count = remote.stackCount;
                }
            }
            inventoryData->inventory.setSlotStack(slot, stack);
        }
        break;
    }
}

void GameClient::handleContainerSnapshot(const net::ContainerSnapshotMessage& snapshot) {
    if (snapshot.containerId == 0) {
        return;
    }
    m_closedContainerIds.erase(snapshot.containerId);
    m_containerSnapshots[snapshot.containerId] = snapshot;
}

void GameClient::handleContainerClose(const net::ContainerCloseMessage& close) {
    if (close.containerId == 0) {
        return;
    }
    m_containerSnapshots.erase(close.containerId);
    m_closedContainerIds.insert(close.containerId);
}

const net::ContainerSnapshotMessage* GameClient::findContainerSnapshot(const uint32_t containerId) const {
    const auto it = m_containerSnapshots.find(containerId);
    return it == m_containerSnapshots.end() ? nullptr : &it->second;
}

const net::ContainerSnapshotMessage* GameClient::findContainerSnapshotAt(const glm::ivec3& blockPosition) const {
    for (const auto& [containerId, snapshot] : m_containerSnapshots) {
        static_cast<void>(containerId);
        if (snapshot.blockPosition == blockPosition) {
            return &snapshot;
        }
    }
    return nullptr;
}

bool GameClient::consumeContainerClose(const uint32_t containerId) {
    return m_closedContainerIds.erase(containerId) > 0;
}

void GameClient::handleWorldStateSnapshot(const net::WorldStateSnapshotMessage& snapshot) {
    if (DayNightSystem* dns = m_clientWorld.mutableDayNightSystem()) {
        dns->setTimeOfDay(snapshot.timeOfDay);
    }
    if (WeatherSystem* weather = m_clientWorld.mutableWeatherSystem()) {
        WeatherType type = WeatherType::Clear;
        switch (snapshot.weather) {
        case net::NetworkWeatherType::Rain:
            type = WeatherType::Rain;
            break;
        case net::NetworkWeatherType::Storm:
            type = WeatherType::Storm;
            break;
        case net::NetworkWeatherType::Snow:
            type = WeatherType::Snow;
            break;
        case net::NetworkWeatherType::Clear:
        default:
            type = WeatherType::Clear;
            break;
        }
        weather->setDebugWeatherPresetInstant(type);
    }
}

void GameClient::handlePlayerModeUpdate(const net::PlayerModeUpdateMessage& update) {
    if (update.clientId == m_clientId && m_localModeCallback) {
        m_localModeCallback(update.mode);
    }
}

} // namespace client
