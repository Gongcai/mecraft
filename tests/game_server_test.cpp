#include "server/GameServer.h"
#include "client/GameClient.h"
#include "net/InProcessTransport.h"
#include "world/World.h"
#include "thread/ThreadPool.h"
#include <cassert>
#include <cstdio>

static void testServerInit() {
    server::GameServer server;
    ThreadPool pool(2);
    server.init(1234, &pool, 8);

    // World should be initialized
    assert(&server.world() != nullptr);
    assert(server.world().getRenderDistance() == 8);
    assert(!server.areSpawnChunksReady());
    assert(server.currentTick() == 0);
    std::printf("[PASS] testServerInit\n");
}

static void testAcceptClient() {
    server::GameServer server;
    ThreadPool pool(2);
    server.init(1234, &pool, 8);

    auto [clientTransport, serverTransport] = net::InProcessTransport::createPair();
    server.acceptClient(std::move(serverTransport), 1);

    // Server should have accepted the client without crashing
    assert(server.currentTick() == 0);
    std::printf("[PASS] testAcceptClient\n");
}

static void testClientServerHandshake() {
    server::GameServer server;
    ThreadPool pool(2);
    server.init(1234, &pool, 8);

    client::GameClient client;

    auto [clientTransport, serverTransport] = net::InProcessTransport::createPair();
    server.acceptClient(std::move(serverTransport), 1);
    client.connect(std::move(clientTransport));

    // Process the ClientHello on the server
    server.tick(1.0f / 20.0f);

    // Client should receive ServerHello
    client.receiveMessages();
    assert(client.getClientId() == 1);
    std::printf("[PASS] testClientServerHandshake\n");
}

static void testServerTick() {
    server::GameServer server;
    ThreadPool pool(2);
    server.init(1234, &pool, 8);

    auto [clientTransport, serverTransport] = net::InProcessTransport::createPair();
    server.acceptClient(std::move(serverTransport), 1);

    // Run several ticks to allow chunk generation
    for (int i = 0; i < 60; ++i) {
        server.tick(1.0f / 20.0f);
    }

    assert(server.currentTick() == 60);
    // Server should have loaded some chunks
    assert(!server.world().getActiveChunks().empty());
    std::printf("[PASS] testServerTick\n");
}

static void testChunkStreamingToClient() {
    server::GameServer server;
    ThreadPool pool(2);
    server.init(1234, &pool, 8);

    client::GameClient client;

    auto [clientTransport, serverTransport] = net::InProcessTransport::createPair();
    server.acceptClient(std::move(serverTransport), 1);
    client.connect(std::move(clientTransport));

    // Run enough ticks for spawn chunks to generate
    for (int i = 0; i < 120; ++i) {
        server.tick(1.0f / 20.0f);
        client.receiveMessages();
    }

    // Client should have received chunks
    assert(client.clientWorld().loadedChunkCount() > 0);
    std::printf("[PASS] testChunkStreamingToClient\n");
}

static void testInputRoundTrip() {
    server::GameServer server;
    ThreadPool pool(2);
    server.init(1234, &pool, 8);

    client::GameClient client;

    auto [clientTransport, serverTransport] = net::InProcessTransport::createPair();
    server.acceptClient(std::move(serverTransport), 1);
    client.connect(std::move(clientTransport));

    // Initial handshake
    server.tick(1.0f / 20.0f);
    client.receiveMessages();

    // Client sends input
    client.sendInput(0.016f, glm::vec3(1.0f, 0.0f, 0.0f), glm::vec2(0.0f), false, false, false);

    // Server processes input
    server.tick(1.0f / 20.0f);

    // Client receives snapshot
    client.receiveMessages();

    // The server should have acked the input
    assert(client.lastSnapshot().ackInputSequence >= 1);
    std::printf("[PASS] testInputRoundTrip\n");
}

int main() {
    testServerInit();
    testAcceptClient();
    testClientServerHandshake();
    testServerTick();
    testChunkStreamingToClient();
    testInputRoundTrip();
    std::printf("\nAll GameServer integration tests passed!\n");
    return 0;
}
