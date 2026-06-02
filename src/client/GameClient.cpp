#include "GameClient.h"
#include "../world/chunk/Chunk.h"

namespace client {

GameClient::GameClient() = default;
GameClient::~GameClient() = default;

void GameClient::connect(std::unique_ptr<net::ITransportEndpoint> transport) {
    m_transport = std::move(transport);
    sendHello();
}

void GameClient::initEntityStore(entt::registry& registry, ResourceMgr* resourceMgr) {
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
    input.jump = jump;
    input.sneak = sneak;
    input.sprint = sprint;
    packet.inProcessPayload = input;
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
                                                   static_cast<BlockID>(update.blockId));
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
        default:
            break;
        }
    }
}

void GameClient::handleChunkData(const net::ChunkDataMessage& data) {
    if (data.chunk) {
        m_clientWorld.addChunk(data.chunk);
        ++m_chunksReceived;

        if (!m_spawnChunksReady && m_chunksReceived >= kSpawnChunksThreshold) {
            m_spawnChunksReady = true;
        }
    }
}

void GameClient::handleServerSnapshot(const net::ServerSnapshot& snapshot) {
    m_lastSnapshot = snapshot;
    m_authPosition = snapshot.authoritativePosition;
}

} // namespace client
