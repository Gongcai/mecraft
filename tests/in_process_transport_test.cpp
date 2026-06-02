#include "net/InProcessTransport.h"
#include "net/Protocol.h"
#include <cassert>
#include <cstdio>

static void testCreatePair() {
    auto [client, server] = net::InProcessTransport::createPair();
    assert(client != nullptr);
    assert(server != nullptr);
    std::printf("[PASS] testCreatePair\n");
}

static void testSendFromClientToServer() {
    auto [client, server] = net::InProcessTransport::createPair();

    net::Packet packet;
    packet.channel = net::PacketChannel::ReliableControl;
    packet.type = net::MessageType::ClientHello;
    net::ClientHello hello;
    hello.protocolVersion = 42;
    packet.inProcessPayload = hello;

    client->send(std::move(packet));

    net::Packet received;
    bool got = server->tryReceive(received);
    assert(got);
    assert(received.type == net::MessageType::ClientHello);
    const auto& receivedHello = std::any_cast<const net::ClientHello&>(received.inProcessPayload);
    assert(receivedHello.protocolVersion == 42);
    std::printf("[PASS] testSendFromClientToServer\n");
}

static void testSendFromServerToClient() {
    auto [client, server] = net::InProcessTransport::createPair();

    net::Packet packet;
    packet.channel = net::PacketChannel::ReliableControl;
    packet.type = net::MessageType::ServerHello;
    net::ServerHello hello;
    hello.assignedId = 7;
    hello.spawnPosition = glm::vec3(1.0f, 2.0f, 3.0f);
    packet.inProcessPayload = hello;

    server->send(std::move(packet));

    net::Packet received;
    bool got = client->tryReceive(received);
    assert(got);
    assert(received.type == net::MessageType::ServerHello);
    const auto& receivedHello = std::any_cast<const net::ServerHello&>(received.inProcessPayload);
    assert(receivedHello.assignedId == 7);
    assert(receivedHello.spawnPosition.y == 2.0f);
    std::printf("[PASS] testSendFromServerToClient\n");
}

static void testEmptyQueueReturnsNoPacket() {
    auto [client, server] = net::InProcessTransport::createPair();

    net::Packet received;
    bool got = client->tryReceive(received);
    assert(!got);

    got = server->tryReceive(received);
    assert(!got);
    std::printf("[PASS] testEmptyQueueReturnsNoPacket\n");
}

static void testBidirectionalCommunication() {
    auto [client, server] = net::InProcessTransport::createPair();

    // Client sends input
    net::Packet inputPacket;
    inputPacket.type = net::MessageType::ClientInput;
    net::ClientInput input;
    input.sequence = 1;
    input.dt = 0.016f;
    input.moveInput = glm::vec3(1.0f, 0.0f, 0.0f);
    inputPacket.inProcessPayload = input;
    client->send(std::move(inputPacket));

    // Server sends snapshot
    net::Packet snapshotPacket;
    snapshotPacket.type = net::MessageType::ServerSnapshot;
    net::ServerSnapshot snapshot;
    snapshot.serverTick = 5;
    snapshot.ackInputSequence = 1;
    snapshot.authoritativePosition = glm::vec3(10.0f, 64.0f, 10.0f);
    snapshotPacket.inProcessPayload = snapshot;
    server->send(std::move(snapshotPacket));

    // Server receives input
    net::Packet receivedInput;
    assert(server->tryReceive(receivedInput));
    assert(receivedInput.type == net::MessageType::ClientInput);
    const auto& ci = std::any_cast<const net::ClientInput&>(receivedInput.inProcessPayload);
    assert(ci.sequence == 1);

    // Client receives snapshot
    net::Packet receivedSnapshot;
    assert(client->tryReceive(receivedSnapshot));
    assert(receivedSnapshot.type == net::MessageType::ServerSnapshot);
    const auto& ss = std::any_cast<const net::ServerSnapshot&>(receivedSnapshot.inProcessPayload);
    assert(ss.serverTick == 5);
    assert(ss.authoritativePosition.x == 10.0f);

    // Both queues should now be empty
    assert(!client->tryReceive(receivedSnapshot));
    assert(!server->tryReceive(receivedInput));
    std::printf("[PASS] testBidirectionalCommunication\n");
}

int main() {
    testCreatePair();
    testSendFromClientToServer();
    testSendFromServerToClient();
    testEmptyQueueReturnsNoPacket();
    testBidirectionalCommunication();
    std::printf("\nAll InProcessTransport tests passed!\n");
    return 0;
}
