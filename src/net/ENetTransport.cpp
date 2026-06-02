#include "ENetTransport.h"
#include "PacketCodec.h"
#include <cstdio>
#include <cstring>

#include "enet/enet.h"

namespace net {

ENetTransport::ENetTransport() = default;

ENetTransport::~ENetTransport() {
    disconnect();
    if (m_host) {
        enet_host_destroy(m_host);
        m_host = nullptr;
    }
}

bool ENetTransport::initialize() {
    return enet_initialize() == 0;
}

void ENetTransport::deinitialize() {
    enet_deinitialize();
}

bool ENetTransport::connect(const std::string& host, uint16_t port,
                             size_t channelCount, uint32_t timeoutMs) {
    if (m_host) {
        enet_host_destroy(m_host);
        m_host = nullptr;
    }

    // Create a client host with 1 outgoing connection
    m_host = enet_host_create(nullptr, 1, channelCount, 0, 0);
    if (!m_host) {
        return false;
    }

    ENetAddress address;
    if (enet_address_set_host_new(&address, host.c_str()) != 0) {
        enet_host_destroy(m_host);
        m_host = nullptr;
        return false;
    }
    address.port = port;

    m_peer = enet_host_connect(m_host, &address, channelCount, 0);
    if (!m_peer) {
        enet_host_destroy(m_host);
        m_host = nullptr;
        return false;
    }

    // Wait for connection with timeout
    ENetEvent event;
    if (enet_host_service(m_host, &event, timeoutMs) > 0 &&
        event.type == ENET_EVENT_TYPE_CONNECT) {
        m_isServer = false;
        std::printf("[ENet] Connected to %s:%u\n", host.c_str(), port);
        std::fflush(stdout);
        return true;
    }

    // Connection failed
    enet_peer_reset(m_peer);
    m_peer = nullptr;
    enet_host_destroy(m_host);
    m_host = nullptr;
    return false;
}

bool ENetTransport::listen(uint16_t port, size_t maxClients, size_t channelCount) {
    if (m_host) {
        enet_host_destroy(m_host);
        m_host = nullptr;
    }

    // Create a server host listening on the specified port.
    // Use enet_host_create with address to bind to all interfaces.
    // ENET_HOST_ANY (in6addr_any) listens on both IPv4 and IPv6.
    ENetAddress address;
    memset(&address, 0, sizeof(address));
    // in6addr_any is {0} - all zeros means "any" address
    address.port = port;

    m_host = enet_host_create(&address, maxClients, channelCount, 0, 0);
    if (!m_host) {
        return false;
    }

    m_isServer = true;
    std::printf("[ENet] Listening on port %u maxClients=%zu channels=%zu\n",
                port,
                maxClients,
                channelCount);
    std::fflush(stdout);
    return true;
}

ENetPeer* ENetTransport::acceptConnection() {
    // The actual accept happens in poll() via ENET_EVENT_TYPE_CONNECT
    // This method returns the most recently connected peer
    return m_peer;
}

void ENetTransport::send(Packet packet) {
    if (!m_host) return;

    // For typed payloads, serialize into binary payload for the network path.
    if (packet.inProcessPayload.has_value() && packet.payload.empty()) {
        // Encode typed message into payload based on message type
        std::vector<uint8_t> typedPayload;
        switch (packet.type) {
        case MessageType::ServerHello: {
            if (packet.inProcessPayload.has_value())
                typedPayload = PacketCodec::encodeServerHello(
                    std::any_cast<const ServerHello&>(packet.inProcessPayload));
            break;
        }
        case MessageType::ServerSnapshot: {
            if (packet.inProcessPayload.has_value())
                typedPayload = PacketCodec::encodeServerSnapshot(
                    std::any_cast<const ServerSnapshot&>(packet.inProcessPayload));
            break;
        }
        case MessageType::ClientInput: {
            if (packet.inProcessPayload.has_value())
                typedPayload = PacketCodec::encodeClientInput(
                    std::any_cast<const ClientInput&>(packet.inProcessPayload));
            break;
        }
        case MessageType::ClientHello: {
            if (packet.inProcessPayload.has_value())
                typedPayload = PacketCodec::encodeClientHello(
                    std::any_cast<const ClientHello&>(packet.inProcessPayload));
            break;
        }
        case MessageType::BlockUpdateBatch: {
            if (packet.inProcessPayload.has_value())
                typedPayload = PacketCodec::encodeBlockUpdateBatch(
                    std::any_cast<const BlockUpdateBatchMessage&>(packet.inProcessPayload));
            break;
        }
        case MessageType::ChunkUnload: {
            if (packet.inProcessPayload.has_value())
                typedPayload = PacketCodec::encodeChunkUnload(
                    std::any_cast<const ChunkUnloadMessage&>(packet.inProcessPayload));
            break;
        }
        case MessageType::EntitySpawn: {
            if (packet.inProcessPayload.has_value())
                typedPayload = PacketCodec::encodeEntitySpawn(
                    std::any_cast<const EntitySpawnMessage&>(packet.inProcessPayload));
            break;
        }
        case MessageType::EntityDespawn: {
            if (packet.inProcessPayload.has_value())
                typedPayload = PacketCodec::encodeEntityDespawn(
                    std::any_cast<const EntityDespawnMessage&>(packet.inProcessPayload));
            break;
        }
        case MessageType::EntitySnapshot: {
            if (packet.inProcessPayload.has_value())
                typedPayload = PacketCodec::encodeEntitySnapshot(
                    std::any_cast<const EntitySnapshotMessage&>(packet.inProcessPayload));
            break;
        }
        case MessageType::ClientViewConfig: {
            if (packet.inProcessPayload.has_value())
                typedPayload = PacketCodec::encodeClientViewConfig(
                    std::any_cast<const ClientViewConfig&>(packet.inProcessPayload));
            break;
        }
        case MessageType::ClientBlockAction: {
            if (packet.inProcessPayload.has_value())
                typedPayload = PacketCodec::encodeClientBlockAction(
                    std::any_cast<const ClientBlockAction&>(packet.inProcessPayload));
            break;
        }
        case MessageType::ChunkData: {
            if (packet.inProcessPayload.has_value())
                typedPayload = PacketCodec::encodeChunkData(
                    std::any_cast<const ChunkDataMessage&>(packet.inProcessPayload));
            break;
        }
        default:
            break;
        }

        if (!typedPayload.empty()) {
            packet.payload = std::move(typedPayload);
        }
    }

    // Encode the packet into binary
    std::vector<uint8_t> data = PacketCodec::encode(packet);

    if (data.size() <= 6) return;  // Header only, no payload

    const auto [channelId, flags] = mapChannel(packet.channel);
    ENetPacket* enetPacket = enet_packet_create(data.data(), data.size(), flags);

    if (m_isServer) {
        // Server: broadcast to all connected peers
        enet_host_broadcast(m_host, channelId, enetPacket);
    } else if (m_peer) {
        // Client: send to server
        enet_peer_send(m_peer, channelId, enetPacket);
    }
    enet_host_flush(m_host);
}

bool ENetTransport::tryReceive(Packet& out) {
    poll();

    std::lock_guard lock(m_receiveMutex);
    if (m_receiveQueue.empty()) {
        return false;
    }
    out = std::move(m_receiveQueue.front());
    m_receiveQueue.pop();
    return true;
}

void ENetTransport::poll() {
    if (!m_host) return;

    ENetEvent event;
    while (enet_host_service(m_host, &event, 0) > 0) {
        switch (event.type) {
        case ENET_EVENT_TYPE_CONNECT:
            if (m_isServer) {
                // Server: new client connected
                m_peer = event.peer;
                char host[256] = {};
                enet_address_get_host_ip_new(&event.peer->address, host, sizeof(host));
                std::printf("[ENet] Client connected from %s:%u\n",
                            host,
                            event.peer->address.port);
                std::fflush(stdout);
            }
            break;

        case ENET_EVENT_TYPE_RECEIVE: {
            // Decode the received data into a Packet
            Packet packet;
            if (PacketCodec::decode(event.packet->data, event.packet->dataLength, packet)) {
                switch (packet.type) {
                case MessageType::ServerHello: {
                    ServerHello msg;
                    if (PacketCodec::decodeServerHello(packet.payload.data(), packet.payload.size(), msg)) {
                        packet.inProcessPayload = msg;
                    }
                    break;
                }
                case MessageType::ServerSnapshot: {
                    ServerSnapshot msg;
                    if (PacketCodec::decodeServerSnapshot(packet.payload.data(), packet.payload.size(), msg)) {
                        packet.inProcessPayload = msg;
                    }
                    break;
                }
                case MessageType::ClientInput: {
                    ClientInput msg;
                    if (PacketCodec::decodeClientInput(packet.payload.data(), packet.payload.size(), msg)) {
                        packet.inProcessPayload = msg;
                    }
                    break;
                }
                case MessageType::ClientHello: {
                    ClientHello msg;
                    if (PacketCodec::decodeClientHello(packet.payload.data(), packet.payload.size(), msg)) {
                        packet.inProcessPayload = msg;
                    }
                    break;
                }
                case MessageType::BlockUpdateBatch: {
                    BlockUpdateBatchMessage msg;
                    if (PacketCodec::decodeBlockUpdateBatch(packet.payload.data(), packet.payload.size(), msg)) {
                        packet.inProcessPayload = std::move(msg);
                    }
                    break;
                }
                case MessageType::ChunkUnload: {
                    ChunkUnloadMessage msg;
                    if (PacketCodec::decodeChunkUnload(packet.payload.data(), packet.payload.size(), msg)) {
                        packet.inProcessPayload = msg;
                    }
                    break;
                }
                case MessageType::ChunkData: {
                    ChunkDataMessage msg;
                    if (PacketCodec::decodeChunkData(packet.payload.data(), packet.payload.size(), msg)) {
                        packet.inProcessPayload = std::move(msg);
                    }
                    break;
                }
                case MessageType::EntitySpawn: {
                    EntitySpawnMessage msg;
                    if (PacketCodec::decodeEntitySpawn(packet.payload.data(), packet.payload.size(), msg)) {
                        packet.inProcessPayload = msg;
                    }
                    break;
                }
                case MessageType::EntityDespawn: {
                    EntityDespawnMessage msg;
                    if (PacketCodec::decodeEntityDespawn(packet.payload.data(), packet.payload.size(), msg)) {
                        packet.inProcessPayload = msg;
                    }
                    break;
                }
                case MessageType::EntitySnapshot: {
                    EntitySnapshotMessage msg;
                    if (PacketCodec::decodeEntitySnapshot(packet.payload.data(), packet.payload.size(), msg)) {
                        packet.inProcessPayload = std::move(msg);
                    }
                    break;
                }
                case MessageType::ClientViewConfig: {
                    ClientViewConfig msg;
                    if (PacketCodec::decodeClientViewConfig(packet.payload.data(), packet.payload.size(), msg)) {
                        packet.inProcessPayload = msg;
                    }
                    break;
                }
                case MessageType::ClientBlockAction: {
                    ClientBlockAction msg;
                    if (PacketCodec::decodeClientBlockAction(packet.payload.data(), packet.payload.size(), msg)) {
                        packet.inProcessPayload = msg;
                    }
                    break;
                }
                default:
                    break;
                }
                std::lock_guard lock(m_receiveMutex);
                m_receiveQueue.push(std::move(packet));
            }
            enet_packet_destroy(event.packet);
            break;
        }

        case ENET_EVENT_TYPE_DISCONNECT:
            if (event.peer == m_peer) {
                std::printf("[ENet] Peer disconnected\n");
                std::fflush(stdout);
                m_peer = nullptr;
            }
            break;

        case ENET_EVENT_TYPE_DISCONNECT_TIMEOUT:
            if (event.peer == m_peer) {
                std::printf("[ENet] Peer disconnect timeout\n");
                std::fflush(stdout);
                m_peer = nullptr;
            }
            break;

        default:
            break;
        }
    }
}

uint32_t ENetTransport::getRtt() const {
    if (m_peer) {
        return m_peer->roundTripTime;
    }
    return 0;
}

std::string ENetTransport::getRemoteAddress() const {
    if (m_peer) {
        char host[256];
        enet_address_get_host_ip_new(&m_peer->address, host, sizeof(host));
        return std::string(host) + ":" + std::to_string(m_peer->address.port);
    }
    return "not connected";
}

uint16_t ENetTransport::getLocalPort() const {
    return m_host ? m_host->address.port : 0;
}

void ENetTransport::disconnect() {
    if (m_peer) {
        enet_peer_disconnect(m_peer, 0);
        // Flush pending disconnect
        ENetEvent event;
        while (enet_host_service(m_host, &event, 3000) > 0) {
            if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
                break;
            }
        }
        enet_peer_reset(m_peer);
        m_peer = nullptr;
    }
}

void ENetTransport::sendToPeer(ENetPeer* peer, Packet packet) {
    if (!m_host || !peer) return;

    std::vector<uint8_t> data = PacketCodec::encode(packet);
    if (data.size() <= 6) return;

    const auto [channelId, flags] = mapChannel(packet.channel);
    ENetPacket* enetPacket = enet_packet_create(data.data(), data.size(), flags);
    enet_peer_send(peer, channelId, enetPacket);
}

ENetTransport::ChannelMapping ENetTransport::mapChannel(PacketChannel channel) {
    switch (channel) {
    case PacketChannel::ReliableControl:
        return {0, ENET_PACKET_FLAG_RELIABLE};
    case PacketChannel::ReliableWorld:
        return {1, ENET_PACKET_FLAG_RELIABLE};
    case PacketChannel::UnreliableState:
        return {2, 0};  // Unreliable, no flags
    case PacketChannel::ReliableChat:
        return {3, ENET_PACKET_FLAG_RELIABLE};
    default:
        return {0, ENET_PACKET_FLAG_RELIABLE};
    }
}

} // namespace net
