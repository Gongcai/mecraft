#include "client/ClientWorld.h"
#include "world/chunk/Chunk.h"
#include "world/block/Block.h"
#include <cassert>
#include <cstdio>

static void testInitialState() {
    client::ClientWorld cw;
    assert(cw.getActiveChunks().empty());
    assert(cw.getActiveChunkRevision() == 1);
    assert(cw.asWorld() == nullptr);
    assert(cw.getRenderDistance() == 16);
    assert(cw.loadedChunkCount() == 0);
    std::printf("[PASS] testInitialState\n");
}

static void testAddChunk() {
    client::ClientWorld cw;
    auto chunk = std::make_shared<Chunk>(0, 0);
    cw.addChunk(chunk);

    assert(cw.loadedChunkCount() == 1);
    assert(cw.getActiveChunkRevision() == 2);  // Incremented from 1
    assert(cw.getActiveChunks().size() == 1);
    std::printf("[PASS] testAddChunk\n");
}

static void testRemoveChunk() {
    client::ClientWorld cw;
    auto chunk = std::make_shared<Chunk>(0, 0);
    cw.addChunk(chunk);
    assert(cw.loadedChunkCount() == 1);

    cw.removeChunk(0, 0);
    assert(cw.loadedChunkCount() == 0);
    assert(cw.getActiveChunkRevision() == 3);  // Incremented twice (add + remove)
    std::printf("[PASS] testRemoveChunk\n");
}

static void testRemoveNonexistentChunk() {
    client::ClientWorld cw;
    cw.removeChunk(5, 5);  // Should not crash
    assert(cw.getActiveChunkRevision() == 1);  // No change
    std::printf("[PASS] testRemoveNonexistentChunk\n");
}

static void testGetBlockFromClientWorld() {
    client::ClientWorld cw;
    auto chunk = std::make_shared<Chunk>(0, 0);
    cw.addChunk(chunk);

    // Default block should be air (0)
    BlockID block = cw.getBlock(0, 0, 0);
    assert(block == BlockIds::AIR);

    // Out-of-range Y should return air
    block = cw.getBlock(0, -1, 0);
    assert(block == BlockIds::AIR);

    block = cw.getBlock(0, 256, 0);
    assert(block == BlockIds::AIR);
    std::printf("[PASS] testGetBlockFromClientWorld\n");
}

static void testIsChunkLoadedForBlock() {
    client::ClientWorld cw;
    assert(!cw.isChunkLoadedForBlock(0, 0, 0));

    auto chunk = std::make_shared<Chunk>(0, 0);
    cw.addChunk(chunk);
    assert(cw.isChunkLoadedForBlock(0, 0, 0));
    assert(cw.isChunkLoadedForBlock(15, 128, 15));  // Same chunk

    // Different chunk (1, 0) should not be loaded
    assert(!cw.isChunkLoadedForBlock(16, 0, 0));
    std::printf("[PASS] testIsChunkLoadedForBlock\n");
}

static void testGetChunkCoords() {
    client::ClientWorld cw;
    auto coords = cw.getChunkCoords(0, 0);
    assert(coords.x == 0 && coords.y == 0);

    coords = cw.getChunkCoords(15, 15);
    assert(coords.x == 0 && coords.y == 0);

    coords = cw.getChunkCoords(16, 16);
    assert(coords.x == 1 && coords.y == 1);

    coords = cw.getChunkCoords(-1, -1);
    assert(coords.x == -1 && coords.y == -1);
    std::printf("[PASS] testGetChunkCoords\n");
}

static void testSetRenderDistance() {
    client::ClientWorld cw;
    assert(cw.getRenderDistance() == 16);
    cw.setRenderDistance(8);
    assert(cw.getRenderDistance() == 8);
    std::printf("[PASS] testSetRenderDistance\n");
}

static void testWeatherDayNightProxy() {
    client::ClientWorld cw;
    assert(cw.dayNightSystem() == nullptr);
    assert(cw.weatherSystem() == nullptr);

    DayNightSystem dns;
    WeatherSystem ws;
    cw.setDayNightSystem(&dns);
    cw.setWeatherSystem(&ws);
    assert(cw.dayNightSystem() == &dns);
    assert(cw.weatherSystem() == &ws);
    std::printf("[PASS] testWeatherDayNightProxy\n");
}

int main() {
    testInitialState();
    testAddChunk();
    testRemoveChunk();
    testRemoveNonexistentChunk();
    testGetBlockFromClientWorld();
    testIsChunkLoadedForBlock();
    testGetChunkCoords();
    testSetRenderDistance();
    testWeatherDayNightProxy();
    std::printf("\nAll ClientWorld tests passed!\n");
    return 0;
}
