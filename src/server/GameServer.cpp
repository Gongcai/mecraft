#include "GameServer.h"
#include "../world/World.h"
#include "../thread/ThreadPool.h"
#include <cmath>

namespace server {

GameServer::GameServer() = default;
GameServer::~GameServer() = default;

void GameServer::init(uint32_t seed, ThreadPool* threadPool, int renderDistance) {
    m_world.setRenderDistance(renderDistance);
    m_world.setThreadPool(threadPool);
    m_world.init(seed);

    // Register block change callback to collect dirty blocks for BlockUpdateBatch
    m_world.setBlockChangeCallback([this](int x, int y, int z, BlockID newBlockId) {
        m_pendingBlockUpdates.push_back({x, y, z, static_cast<uint16_t>(newBlockId)});
    });

    // Compute spawn position from world surface
    constexpr float kSpawnHeightOffset = 2.0f;
    const int surfaceY = m_world.getSurfaceY(0, 0);
    m_spawnPosition = glm::vec3(0.0f, static_cast<float>(surfaceY + kSpawnHeightOffset), 0.0f);
}

void GameServer::acceptClient(std::unique_ptr<net::ITransportEndpoint> transport, net::ClientId id) {
    ConnectedClient client;
    client.id = id;
    client.transport = std::move(transport);
    client.lastSentChunkRevision = 0;
    m_clients.push_back(std::move(client));
}

void GameServer::tick(float dt) {
    (void)dt;

    // Update world: load/unload chunks around the spawn position (or player positions).
    // For Phase 1, we use the spawn position. Phase 3 introduces ticket-based loading.
    m_world.update(m_spawnPosition);

    // Process incoming client messages
    processClientMessages();

    // Send new chunks to clients
    sendNewChunksToClients();

    // Send authoritative snapshots to clients
    sendSnapshotsToClients();

    // Send pending block updates to clients
    sendBlockUpdatesToClients();

    // Check if spawn chunks are ready
    if (!m_spawnChunksReady) {
        checkSpawnChunksReady();
    }

    ++m_currentTick;
}

void GameServer::processClientMessages() {
    for (auto& client : m_clients) {
        net::Packet packet;
        while (client.transport->tryReceive(packet)) {
            switch (packet.type) {
            case net::MessageType::ClientHello: {
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
                break;
            }
            case net::MessageType::ClientInput: {
                if (packet.inProcessPayload.has_value()) {
                    const auto& input = std::any_cast<const net::ClientInput&>(packet.inProcessPayload);
                    client.lastAckedInput = input.sequence;
                    // For Phase 1, we don't apply input physics on the server yet.
                    // The server just tracks the last acked sequence.
                }
                break;
            }
            case net::MessageType::ClientReady:
                // Client is ready to receive world data; nothing special needed.
                break;
            default:
                break;
            }
        }
    }
}

void GameServer::sendNewChunksToClients() {
    const auto& activeChunks = m_world.getActiveChunks();
    const uint64_t currentRevision = m_world.getActiveChunkRevision();

    for (auto& client : m_clients) {
        if (client.lastSentChunkRevision == currentRevision) {
            continue;  // No new chunks since last send
        }

        for (const auto& [key, chunk] : activeChunks) {
            if (chunk && client.sentChunks.find(key) == client.sentChunks.end()) {
                sendChunkDataToClient(client, chunk->m_chunkX, chunk->m_chunkZ);
                client.sentChunks.insert(key);
            }
        }

        client.lastSentChunkRevision = currentRevision;
    }
}

void GameServer::sendSnapshotsToClients() {
    for (auto& client : m_clients) {
        net::Packet packet;
        packet.channel = net::PacketChannel::UnreliableState;
        packet.type = net::MessageType::ServerSnapshot;
        net::ServerSnapshot snapshot;
        snapshot.serverTick = m_currentTick;
        snapshot.ackInputSequence = client.lastAckedInput;
        snapshot.authoritativePosition = m_spawnPosition;
        snapshot.authoritativeVelocity = glm::vec3(0.0f);
        packet.inProcessPayload = snapshot;
        client.transport->send(std::move(packet));
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
        // Send a copy to each client
        net::Packet clientPacket = packet;
        client.transport->send(std::move(clientPacket));
    }

    m_pendingBlockUpdates.clear();
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
