#ifndef MECRAFT_NET_ENET_TRANSPORT_H
#define MECRAFT_NET_ENET_TRANSPORT_H

#include "Transport.h"
#include <cstdint>
#include <string>
#include <queue>
#include <mutex>

// Forward-declare ENet types to avoid including <enet/enet.h> in the header.
// This prevents Windows SDK's global `byte` typedef from conflicting with std::byte.
struct _ENetHost;
typedef _ENetHost ENetHost;
struct _ENetPeer;
typedef _ENetPeer ENetPeer;

namespace net {

/// ENet-based UDP transport endpoint.
/// Implements ITransportEndpoint for real network communication.
/// Uses ENet's reliable/unreliable channels mapped to PacketChannel.
class ENetTransport : public ITransportEndpoint {
public:
    ENetTransport();
    ~ENetTransport() override;

    /// Initialize ENet library. Must be called once before creating any transport.
    static bool initialize();

    /// Deinitialize ENet library. Call once at shutdown.
    static void deinitialize();

    /// Connect to a remote server.
    /// @param host Hostname or IP address
    /// @param port Port number
    /// @param channelCount Number of channels (default 4)
    /// @param timeoutMs Connection timeout in milliseconds
    /// @return true if connection initiated successfully
    bool connect(const std::string& host, uint16_t port,
                 size_t channelCount = 4, uint32_t timeoutMs = 5000);

    /// Start listening for incoming connections (server mode).
    /// @param port Port to listen on
    /// @param maxClients Maximum number of clients
    /// @param channelCount Number of channels per client
    /// @return true if server started successfully
    bool listen(uint16_t port, size_t maxClients = 32, size_t channelCount = 4);

    /// Accept a pending connection (call after poll() returns a connect event).
    /// @return The accepted peer, or nullptr if no pending connection
    ENetPeer* acceptConnection();

    // ITransportEndpoint interface
    void send(Packet packet) override;
    bool tryReceive(Packet& out) override;

    /// Poll for network events. Must be called regularly (e.g., each frame/tick).
    /// Processes incoming data and connection events.
    void poll();

    /// Check if connected to a remote peer.
    [[nodiscard]] bool isConnected() const { return m_peer != nullptr; }

    /// Get the RTT to the connected peer in milliseconds.
    [[nodiscard]] uint32_t getRtt() const;

    /// Get the remote address as a string.
    [[nodiscard]] std::string getRemoteAddress() const;

    /// Get the local listening/bound port, or 0 if not available.
    [[nodiscard]] uint16_t getLocalPort() const;

    /// Disconnect from the remote peer.
    void disconnect();

private:
    /// Send a packet to a specific peer.
    void sendToPeer(ENetPeer* peer, Packet packet);

    /// Map PacketChannel to ENet channel ID and flags.
    struct ChannelMapping {
        uint8_t channelId;
        uint32_t flags;
    };
    static ChannelMapping mapChannel(PacketChannel channel);

    ENetHost* m_host = nullptr;
    ENetPeer* m_peer = nullptr;  // Client: connected peer. Server: not used.
    bool m_isServer = false;

    std::queue<Packet> m_receiveQueue;
    std::mutex m_receiveMutex;
};

} // namespace net

#endif // MECRAFT_NET_ENET_TRANSPORT_H
