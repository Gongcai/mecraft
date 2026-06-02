#ifndef MECRAFT_NET_TRANSPORT_H
#define MECRAFT_NET_TRANSPORT_H

#include "Protocol.h"

namespace net {

/// Abstract transport endpoint for sending and receiving protocol packets.
/// Implementations: InProcessTransport (Phase 1), ENetTransport (Phase 6).
class ITransportEndpoint {
public:
    virtual ~ITransportEndpoint() = default;

    /// Send a packet to the remote side.
    virtual void send(Packet packet) = 0;

    /// Try to receive a packet from the remote side.
    /// @param out Filled with the received packet if available.
    /// @return true if a packet was received, false if the queue is empty.
    virtual bool tryReceive(Packet& out) = 0;
};

} // namespace net

#endif // MECRAFT_NET_TRANSPORT_H
