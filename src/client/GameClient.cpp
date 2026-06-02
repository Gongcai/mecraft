#include "GameClient.h"
#include "../ecs/GameplayRegistry.h"
#include "../world/chunk/Chunk.h"
#include <cstdio>

namespace client {

GameClient::GameClient() = default;
GameClient::~GameClient() = default;

void GameClient::connect(std::unique_ptr<net::ITransportEndpoint> transport) {
    m_transport = std::move(transport);
    std::printf("[Client] Transport connected; sending hello\n");
    std::fflush(stdout);
    sendHello();
}

void GameClient::initEntityStore(entt::registry& registry, ResourceMgr* resourceMgr) {
    m_entityStore.init(registry, resourceMgr);
}

void GameClient::initEntityStore(ecs::GameplayRegistry& registry, ResourceMgr* resourceMgr) {
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
                           float pitch) {
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
                std::printf("[Client] ServerHello id=%u spawn=(%.1f, %.1f, %.1f)\n",
                            m_clientId,
                            m_authPosition.x,
                            m_authPosition.y,
                            m_authPosition.z);
                std::fflush(stdout);
            } else {
                std::printf("[Client] Received ServerHello without decoded payload bytes=%zu\n",
                            packet.payload.size());
                std::fflush(stdout);
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
                                                   static_cast<BlockID>(update.blockId),
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
        case net::MessageType::EntitySnapshot: {
            if (packet.inProcessPayload.has_value()) {
                const auto& msg = std::any_cast<const net::EntitySnapshotMessage&>(packet.inProcessPayload);
                m_entityStore.handleSnapshot(msg);
            }
            break;
        }
        case net::MessageType::InventorySnapshot: {
            // Inventory snapshot received from server.
            // For Phase 5, this is a placeholder — the client acknowledges but
            // doesn't yet apply the inventory (ECS runs on the same registry in-process).
            // In Phase 6 (real networking), this will update the client-side inventory display.
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
            std::printf("[Client] Received ChunkData chunk=(%d,%d) count=%d loaded=%zu\n",
                        data.chunkX,
                        data.chunkZ,
                        m_chunksReceived,
                        m_clientWorld.loadedChunkCount());
            std::fflush(stdout);
        }

        if (!m_spawnChunksReady && m_chunksReceived >= kSpawnChunksThreshold) {
            m_spawnChunksReady = true;
            std::printf("[Client] Spawn chunks ready after %d chunks\n", m_chunksReceived);
            std::fflush(stdout);
        }
    }
}

void GameClient::handleServerSnapshot(const net::ServerSnapshot& snapshot) {
    m_lastSnapshot = snapshot;
    m_authPosition = snapshot.authoritativePosition;
}

} // namespace client
