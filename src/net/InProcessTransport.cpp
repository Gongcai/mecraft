#include "InProcessTransport.h"

namespace net {

std::pair<std::unique_ptr<InProcessTransport>, std::unique_ptr<InProcessTransport>> InProcessTransport::createPair() {
    auto mutex = std::make_shared<std::mutex>();
    auto queueA = std::make_shared<std::queue<Packet>>();
    auto queueB = std::make_shared<std::queue<Packet>>();

    // Endpoint A: inbox=queueB, outbox=queueA (sends to B's inbox)
    // Endpoint B: inbox=queueA, outbox=queueB (sends to A's inbox)
    auto endpointA = std::unique_ptr<InProcessTransport>(new InProcessTransport(mutex, queueB, queueA));
    auto endpointB = std::unique_ptr<InProcessTransport>(new InProcessTransport(mutex, queueA, queueB));

    return {std::move(endpointA), std::move(endpointB)};
}

InProcessTransport::InProcessTransport(std::shared_ptr<std::mutex> mutex, std::shared_ptr<std::queue<Packet>> inbox,
                                       std::shared_ptr<std::queue<Packet>> outbox)
    : m_mutex(std::move(mutex)), m_inbox(std::move(inbox)), m_outbox(std::move(outbox)) {}

void InProcessTransport::send(Packet packet) {
    std::lock_guard lock(*m_mutex);
    m_outbox->push(std::move(packet));
}

bool InProcessTransport::tryReceive(Packet& out) {
    std::lock_guard lock(*m_mutex);
    if (m_inbox->empty()) {
        return false;
    }
    out = std::move(m_inbox->front());
    m_inbox->pop();
    return true;
}

} // namespace net
