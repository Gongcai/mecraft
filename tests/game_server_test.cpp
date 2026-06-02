#include "server/GameServer.h"
#include "client/GameClient.h"
#include "net/InProcessTransport.h"
#include "net/ENetTransport.h"
#include "world/World.h"
#include "world/block/Block.h"
#include "thread/ThreadPool.h"
#include <cassert>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

struct ServerHarness {
    explicit ServerHarness(int renderDistance = 8) {
        server.init(1234, nullptr, renderDistance);
    }

    server::GameServer server;
};

static void testServerInit() {
    ServerHarness harness;

    // World should be initialized
    assert(&harness.server.world() != nullptr);
    assert(harness.server.world().getRenderDistance() == 8);
    assert(!harness.server.areSpawnChunksReady());
    assert(harness.server.currentTick() == 0);
    std::printf("[PASS] testServerInit\n");
}

static void testAcceptClient() {
    ServerHarness harness;

    auto [clientTransport, serverTransport] = net::InProcessTransport::createPair();
    harness.server.acceptClient(std::move(serverTransport), 1);

    // Server should have accepted the client without crashing
    assert(harness.server.currentTick() == 0);
    std::printf("[PASS] testAcceptClient\n");
}

static void testClientServerHandshake() {
    ServerHarness harness;

    client::GameClient client;

    auto [clientTransport, serverTransport] = net::InProcessTransport::createPair();
    harness.server.acceptClient(std::move(serverTransport), 1);
    client.connect(std::move(clientTransport));

    // Process the ClientHello on the server
    harness.server.tick(1.0f / 20.0f);

    // Client should receive ServerHello
    client.receiveMessages();
    assert(client.getClientId() == 1);
    std::printf("[PASS] testClientServerHandshake\n");
}

static void testServerTick() {
    ServerHarness harness;

    auto [clientTransport, serverTransport] = net::InProcessTransport::createPair();
    harness.server.acceptClient(std::move(serverTransport), 1);

    // Run several ticks to allow chunk generation
    for (int i = 0; i < 60; ++i) {
        harness.server.tick(1.0f / 20.0f);
    }

    assert(harness.server.currentTick() == 60);
    // Server should have loaded some chunks
    assert(!harness.server.world().getActiveChunks().empty());
    std::printf("[PASS] testServerTick\n");
}

static void testChunkStreamingToClient() {
    ServerHarness harness;

    client::GameClient client;

    auto [clientTransport, serverTransport] = net::InProcessTransport::createPair();
    harness.server.acceptClient(std::move(serverTransport), 1);
    client.connect(std::move(clientTransport));

    // Run enough ticks for spawn chunks to generate
    for (int i = 0; i < 120; ++i) {
        harness.server.tick(1.0f / 20.0f);
        client.receiveMessages();
    }

    // Client should have received chunks
    assert(client.clientWorld().loadedChunkCount() > 0);
    std::printf("[PASS] testChunkStreamingToClient\n");
}

static void testInputRoundTrip() {
    ServerHarness harness;

    client::GameClient client;

    auto [clientTransport, serverTransport] = net::InProcessTransport::createPair();
    harness.server.acceptClient(std::move(serverTransport), 1);
    client.connect(std::move(clientTransport));

    // Initial handshake
    harness.server.tick(1.0f / 20.0f);
    client.receiveMessages();

    // Client sends input
    client.sendInput(0.016f, glm::vec3(1.0f, 0.0f, 0.0f), glm::vec2(0.0f), false, false, false);

    // Server processes input
    harness.server.tick(1.0f / 20.0f);

    // Client receives snapshot
    client.receiveMessages();

    // The server should have acked the input
    assert(client.lastSnapshot().ackInputSequence >= 1);
    std::printf("[PASS] testInputRoundTrip\n");
}

static void testENetChunkStreamingToClient() {
    if (!net::ENetTransport::initialize()) {
        std::fprintf(stderr, "[FAIL] ENet initialize failed\n");
        std::abort();
    }

    ServerHarness harness;

    auto serverTransport = std::make_unique<net::ENetTransport>();
    if (!serverTransport->listen(0, 1, 4)) {
        std::fprintf(stderr, "[FAIL] ENet listen failed\n");
        std::abort();
    }
    const uint16_t port = serverTransport->getLocalPort();
    if (port == 0) {
        std::fprintf(stderr, "[FAIL] ENet local port was not assigned\n");
        std::abort();
    }
    harness.server.acceptClient(std::move(serverTransport), 1);

    auto clientTransport = std::make_unique<net::ENetTransport>();
    std::atomic<bool> connectDone{false};
    std::atomic<bool> connectOk{false};
    std::thread connectThread([&]() {
        connectOk.store(clientTransport->connect("127.0.0.1", port, 4, 2000));
        connectDone.store(true);
    });
    for (int i = 0; i < 250 && !connectDone.load(); ++i) {
        harness.server.tick(1.0f / 20.0f);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    connectThread.join();
    if (!connectOk.load()) {
        std::fprintf(stderr, "[FAIL] ENet connect failed on port %u\n", port);
        std::abort();
    }

    client::GameClient client;
    client.connect(std::move(clientTransport));
    client.sendViewConfig(8);

    for (int i = 0; i < 500 && client.clientWorld().loadedChunkCount() == 0; ++i) {
        harness.server.tick(1.0f / 20.0f);
        client.receiveMessages();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (client.getClientId() != 1 || client.clientWorld().loadedChunkCount() == 0) {
        std::fprintf(stderr,
                     "[FAIL] ENet streaming failed id=%u loaded=%zu active=%zu tick=%u\n",
                     client.getClientId(),
                     client.clientWorld().loadedChunkCount(),
                     harness.server.world().getActiveChunks().size(),
                     harness.server.currentTick());
        std::abort();
    }
    std::printf("[PASS] testENetChunkStreamingToClient\n");

    net::ENetTransport::deinitialize();
}

int main() {
    BlockRegistry::init(nullptr);

    testServerInit();
    testAcceptClient();
    testClientServerHandshake();
    testServerTick();
    testChunkStreamingToClient();
    testInputRoundTrip();
    testENetChunkStreamingToClient();
    std::printf("\nAll GameServer integration tests passed!\n");
    return 0;
}
