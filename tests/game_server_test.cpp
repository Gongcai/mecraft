#include "server/GameServer.h"
#include "client/GameClient.h"
#include "net/InProcessTransport.h"
#include "net/ENetTransport.h"
#include "net/PacketCodec.h"
#include "world/World.h"
#include "world/block/Block.h"
#include "renderer/mesh/ChunkMesher.h"
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

static void testChunkDataDecodeMarksRenderableSubChunks() {
    auto source = std::make_shared<Chunk>(0, 0);
    for (int y = 48; y < 56; ++y) {
        for (int z = 0; z < Chunk::SIZE_Z; ++z) {
            for (int x = 0; x < Chunk::SIZE_X; ++x) {
                source->setBlockFast(x, y, z, BlockIds::STONE);
            }
        }
    }
    source->seedInitialLightMap();

    net::ChunkDataMessage message;
    message.chunkX = 0;
    message.chunkZ = 0;
    message.revision = 1;
    message.chunk = source;

    const std::vector<uint8_t> encoded = net::PacketCodec::encodeChunkData(message);

    net::ChunkDataMessage decoded;
    if (!net::PacketCodec::decodeChunkData(encoded.data(), encoded.size(), decoded) || !decoded.chunk) {
        std::fprintf(stderr, "[FAIL] ChunkData decode failed\n");
        std::abort();
    }

    const int scy = Chunk::toSubChunkIndex(48);
    const SubChunk* subChunk = decoded.chunk->getSubChunk(scy);
    if (subChunk == nullptr || subChunk->getType() == SubChunkType::Air || !decoded.chunk->isSubChunkDirty(scy)) {
        std::fprintf(stderr,
                     "[FAIL] Decoded chunk subchunk not renderable dirty scy=%d hasSub=%d type=%d dirty=%d\n",
                     scy,
                     subChunk != nullptr ? 1 : 0,
                     subChunk != nullptr ? static_cast<int>(subChunk->getType()) : -1,
                     decoded.chunk->isSubChunkDirty(scy) ? 1 : 0);
        std::abort();
    }

    const SubChunkMeshingSnapshotPtr snapshot =
        ChunkMesher::captureSubChunkSnapshot(*decoded.chunk, scy, nullptr);
    if (!snapshot) {
        std::fprintf(stderr, "[FAIL] Decoded chunk meshing snapshot was null\n");
        std::abort();
    }
    const ChunkMeshData meshData = ChunkMesher::buildSubChunkMeshData(*snapshot);
    const size_t totalVertices =
        meshData.opaqueVertices.size() +
        meshData.cutoutVertices.size() +
        meshData.cutoutDistanceVertices.size() +
        meshData.transparentVertices.size() +
        meshData.waterVertices.size();
    if (totalVertices == 0) {
        std::fprintf(stderr, "[FAIL] Decoded chunk produced an empty terrain mesh\n");
        std::abort();
    }

    std::printf("[PASS] testChunkDataDecodeMarksRenderableSubChunks\n");
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

static void testENetChunkStreamingAfterPreconnectTicks() {
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

    for (int i = 0; i < 240; ++i) {
        harness.server.tick(1.0f / 20.0f);
    }
    if (harness.server.world().getActiveChunks().empty()) {
        std::fprintf(stderr, "[FAIL] Server did not load chunks before connect\n");
        std::abort();
    }

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

    if (client.clientWorld().loadedChunkCount() == 0) {
        std::fprintf(stderr,
                     "[FAIL] ENet preconnect streaming failed loaded=%zu active=%zu tick=%u\n",
                     client.clientWorld().loadedChunkCount(),
                     harness.server.world().getActiveChunks().size(),
                     harness.server.currentTick());
        std::abort();
    }
    std::printf("[PASS] testENetChunkStreamingAfterPreconnectTicks\n");

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
    testChunkDataDecodeMarksRenderableSubChunks();
    testENetChunkStreamingToClient();
    testENetChunkStreamingAfterPreconnectTicks();
    std::printf("\nAll GameServer integration tests passed!\n");
    return 0;
}
