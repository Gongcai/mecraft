#include "GameServer.h"
#include "../world/World.h"
#include "../world/block/Block.h"
#include "../thread/ThreadPool.h"
#include "../ecs/components/Components.h"
#include "../ecs/components/NetworkComponents.h"
#include <cmath>
#include <cstdio>

namespace server {
namespace {
constexpr net::EntityNetId kPlayerNetIdBase = 0x80000000u;
}

GameServer::GameServer() = default;
GameServer::~GameServer() = default;

void GameServer::init(uint32_t seed, ThreadPool* threadPool, int renderDistance) {
    m_world.setRenderDistance(renderDistance);
    m_world.setThreadPool(threadPool);
    m_world.init(seed);

    // Register block change callback to collect dirty blocks for BlockUpdateBatch
    m_world.setBlockChangeCallback([this](int x, int y, int z, BlockID newBlockId) {
        m_pendingBlockUpdates.push_back(makeBlockUpdateEntry(x, y, z, newBlockId));
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
}

void GameServer::acceptClient(std::unique_ptr<net::ITransportEndpoint> transport, net::ClientId id) {
    ConnectedClient client;
    client.id = id;
    client.transport = std::move(transport);
    client.lastPosition = m_spawnPosition;
    client.playerNetId = kPlayerNetIdBase | id;
    m_clients.push_back(std::move(client));
    std::printf("[Server] Accepted transport slot for client %u\n", id);
    std::fflush(stdout);
}

void GameServer::tick(float dt) {
    (void)dt;

    // Process incoming client messages first so block edits and player poses
    // participate in this tick's world/light update before snapshots are sent.
    processClientMessages();

    glm::vec3 loadCenter = m_spawnPosition;
    for (const auto& client : m_clients) {
        if (client.receivedHello) {
            loadCenter = client.lastPosition;
            break;
        }
    }
    m_world.update(loadCenter);

    // Send new chunks to clients
    sendNewChunksToClients();

    // Send authoritative snapshots to clients
    sendSnapshotsToClients();

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
                break;
            }
            case net::MessageType::ClientInput: {
                if (packet.inProcessPayload.has_value()) {
                    const auto& input = std::any_cast<const net::ClientInput&>(packet.inProcessPayload);
                    client.lastAckedInput = input.sequence;
                    client.lastPosition = input.playerPosition;
                    client.lastVelocity = input.playerVelocity;
                    client.lastYaw = input.yaw;
                    client.lastPitch = input.pitch;
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
                    handleClientBlockAction(client, action);
                }
                break;
            }
            default:
                break;
            }
        }
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
        if (!client.receivedHello || client.helloTick == m_currentTick) {
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
    for (auto& update : batch.updates) {
        update.packedLightPatch = makeBlockUpdateEntry(update.x,
                                                       update.y,
                                                       update.z,
                                                       static_cast<BlockID>(update.blockId)).packedLightPatch;
    }
    packet.inProcessPayload = std::move(batch);

    for (auto& client : m_clients) {
        // Send a copy to each client
        net::Packet clientPacket = packet;
        client.transport->send(std::move(clientPacket));
    }

    m_pendingBlockUpdates.clear();
}

net::BlockUpdateEntry GameServer::makeBlockUpdateEntry(const int x, const int y, const int z, const BlockID blockId) const {
    net::BlockUpdateEntry entry;
    entry.x = x;
    entry.y = y;
    entry.z = z;
    entry.blockId = static_cast<uint16_t>(blockId);
    entry.packedLightPatch.reserve(27);
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
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
        if (!receiver.receivedHello || !receiver.transport) {
            continue;
        }

        for (auto& other : m_clients) {
            if (other.id == receiver.id || !other.receivedHello) {
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
        if (!client.receivedHello) {
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
        if (!receiver.receivedHello || !receiver.transport) {
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

    // 3. Build and send entity snapshots (batch of all synced entities)
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

        snapshot.entities.push_back(item);
    }

    if (!snapshot.entities.empty()) {
        net::Packet packet;
        packet.channel = net::PacketChannel::UnreliableState;
        packet.type = net::MessageType::EntitySnapshot;
        packet.inProcessPayload = std::move(snapshot);

        for (auto& client : m_clients) {
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
