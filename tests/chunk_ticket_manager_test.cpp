#include "server/ChunkTicketManager.h"
#include <cassert>
#include <cstdio>

static void testInitialState() {
    ChunkTicketManager mgr;
    mgr.setViewRadius(16);
    mgr.setSimulationRadius(8);

    // At origin, everything within radius should load
    assert(mgr.shouldLoad(0, 0));
    assert(mgr.shouldLoad(1, 0));
    assert(mgr.shouldLoad(-1, 0));
    assert(!mgr.shouldLoad(20, 0));  // Outside viewRadius + 1

    // Ticking requires simulation radius
    assert(mgr.shouldTick(0, 0));
    assert(mgr.shouldTick(7, 0));    // Edge of sim radius
    assert(!mgr.shouldTick(10, 0));  // Outside sim radius

    std::printf("[PASS] testInitialState\n");
}

static void testUnloadHysteresis() {
    ChunkTicketManager mgr;
    mgr.setViewRadius(4);
    mgr.setSimulationRadius(2);
    // loadRadius = 5, unloadRadius = 7

    mgr.updatePlayerPosition(0, 0);

    // Within load radius: should NOT unload
    assert(!mgr.shouldUnload(0, 0));
    assert(!mgr.shouldUnload(4, 0));

    // Between load and unload radius: should NOT unload (hysteresis band)
    assert(!mgr.shouldUnload(5, 0));
    assert(!mgr.shouldUnload(6, 0));

    // Outside unload radius: SHOULD unload
    assert(mgr.shouldUnload(8, 0));
    assert(mgr.shouldUnload(0, 8));

    std::printf("[PASS] testUnloadHysteresis\n");
}

static void testGetChunksToLoad() {
    ChunkTicketManager mgr;
    mgr.setViewRadius(2);
    mgr.setSimulationRadius(1);
    // loadRadius = 3

    mgr.updatePlayerPosition(0, 0);

    std::unordered_set<int64_t> loaded;
    auto chunks = mgr.getChunksToLoad(100, loaded);

    // Should have chunks within radius 3
    assert(!chunks.empty());

    // First chunk should be closest (origin)
    assert(chunks[0].x == 0 && chunks[0].y == 0);

    // All chunks should be within load radius
    for (const auto& c : chunks) {
        int dx = c.x;
        int dz = c.y;
        assert(dx * dx + dz * dz <= 3 * 3);
    }

    std::printf("[PASS] testGetChunksToLoad\n");
}

static void testGetChunksToLoadExcludesLoaded() {
    ChunkTicketManager mgr;
    mgr.setViewRadius(2);
    mgr.updatePlayerPosition(0, 0);

    std::unordered_set<int64_t> loaded;
    auto chunks1 = mgr.getChunksToLoad(100, loaded);
    assert(!chunks1.empty());

    // Mark all as loaded
    for (const auto& c : chunks1) {
        loaded.insert(ChunkTicketManager::chunkKey(c.x, c.y));
    }

    auto chunks2 = mgr.getChunksToLoad(100, loaded);
    assert(chunks2.empty());  // All already loaded

    std::printf("[PASS] testGetChunksToLoadExcludesLoaded\n");
}

static void testGetChunksToUnload() {
    ChunkTicketManager mgr;
    mgr.setViewRadius(4);
    mgr.setSimulationRadius(2);
    // unloadRadius = 7

    mgr.updatePlayerPosition(0, 0);

    std::unordered_set<int64_t> loaded;
    // Add some chunks: some inside, some outside unload radius
    loaded.insert(ChunkTicketManager::chunkKey(0, 0));   // Inside
    loaded.insert(ChunkTicketManager::chunkKey(3, 0));   // Inside
    loaded.insert(ChunkTicketManager::chunkKey(8, 0));   // Outside
    loaded.insert(ChunkTicketManager::chunkKey(0, 10));  // Outside

    auto toUnload = mgr.getChunksToUnload(loaded);
    assert(toUnload.size() == 2);  // Only the two outside

    std::printf("[PASS] testGetChunksToUnload\n");
}

static void testPlayerPositionUpdate() {
    ChunkTicketManager mgr;
    mgr.setViewRadius(2);
    mgr.setSimulationRadius(1);
    // loadRadius = 3

    mgr.updatePlayerPosition(10, 10);

    // Chunk at (10,10) should be loadable
    assert(mgr.shouldLoad(10, 10));
    assert(mgr.shouldLoad(12, 10));
    assert(!mgr.shouldLoad(0, 0));  // Far from player

    // Move player to (0,0)
    mgr.updatePlayerPosition(0, 0);
    assert(mgr.shouldLoad(0, 0));
    assert(!mgr.shouldLoad(10, 10));  // Now far from player

    std::printf("[PASS] testPlayerPositionUpdate\n");
}

int main() {
    testInitialState();
    testUnloadHysteresis();
    testGetChunksToLoad();
    testGetChunksToLoadExcludesLoaded();
    testGetChunksToUnload();
    testPlayerPositionUpdate();
    std::printf("\nAll ChunkTicketManager tests passed!\n");
    return 0;
}
