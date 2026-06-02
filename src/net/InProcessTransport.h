#ifndef MECRAFT_NET_IN_PROCESS_TRANSPORT_H
#define MECRAFT_NET_IN_PROCESS_TRANSPORT_H

#include "Transport.h"
#include <mutex>
#include <queue>
#include <memory>

namespace net {

/// Thread-safe bidirectional in-process transport.
/// Two endpoints share paired queues: sending on one side enqueues to the other's inbox.
class InProcessTransport : public ITransportEndpoint {
public:
    /// Create a connected pair of endpoints.
    /// @return {clientEndpoint, serverEndpoint}
    static std::pair<std::unique_ptr<InProcessTransport>,
                     std::unique_ptr<InProcessTransport>> createPair();

    void send(Packet packet) override;
    bool tryReceive(Packet& out) override;

private:
    InProcessTransport(std::shared_ptr<std::mutex> mutex,
                       std::shared_ptr<std::queue<Packet>> inbox,
                       std::shared_ptr<std::queue<Packet>> outbox);

    std::shared_ptr<std::mutex> m_mutex;
    std::shared_ptr<std::queue<Packet>> m_inbox;
    std::shared_ptr<std::queue<Packet>> m_outbox;
};

} // namespace net

#endif // MECRAFT_NET_IN_PROCESS_TRANSPORT_H
