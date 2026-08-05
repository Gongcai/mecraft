#include "client/ClientWorld.h"
#include "net/Protocol.h"
#include "world/chunk/Chunk.h"
#include "world/block/Block.h"
#include "world/block/BlockStateRegistry.h"
#include "world/block/PropIndices.h"
#include "world/fluid/FluidState.h"
#include "world/WorldRaycast.h"
#include "physics/PhysicsInfo.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>

static void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "[FAIL] %s\n", message);
        std::exit(EXIT_FAILURE);
    }
}

static BlockStateId targetState(const uint16_t powerValue) {
    return BlockStateRegistry::getState(BlockRegistry::requireIdByName("minecraft:target"), PropIndices::POWER,
                                        powerValue);
}

static BlockStateId defaultState(const char* name) {
    return BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName(name));
}

static void testInitialState() {
    client::ClientWorld cw;
    assert(cw.getActiveChunks().empty());
    assert(cw.getActiveChunkRevision() == 1);
    assert(cw.getBlockContentRevision() == 1);
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
    assert(cw.getActiveChunkRevision() == 2); // Incremented from 1
    assert(cw.getBlockContentRevision() == 2); // Incremented from 1
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
    assert(cw.getActiveChunkRevision() == 3); // Incremented twice (add + remove)
    assert(cw.getBlockContentRevision() == 3); // Incremented twice (add + remove)
    std::printf("[PASS] testRemoveChunk\n");
}

static void testRemoveNonexistentChunk() {
    client::ClientWorld cw;
    cw.removeChunk(5, 5); // Should not crash
    assert(cw.getActiveChunkRevision() == 1); // No change
    assert(cw.getBlockContentRevision() == 1); // No change
    std::printf("[PASS] testRemoveNonexistentChunk\n");
}

static void testGetBlockFromClientWorld() {
    client::ClientWorld cw;
    auto chunk = std::make_shared<Chunk>(0, 0);
    cw.addChunk(chunk);

    // Default block should be air (0)
    BlockStateId block = cw.getBlockState(0, 0, 0);
    assert(block == NULL_BLOCK_STATE);

    // Out-of-range Y should return air
    block = cw.getBlockState(0, -1, 0);
    assert(block == NULL_BLOCK_STATE);

    block = cw.getBlockState(0, 256, 0);
    assert(block == NULL_BLOCK_STATE);
    std::printf("[PASS] testGetBlockFromClientWorld\n");
}

static void testIsChunkLoadedForBlock() {
    client::ClientWorld cw;
    assert(!cw.isChunkLoadedForBlock(0, 0, 0));

    auto chunk = std::make_shared<Chunk>(0, 0);
    cw.addChunk(chunk);
    assert(cw.isChunkLoadedForBlock(0, 0, 0));
    assert(cw.isChunkLoadedForBlock(15, 128, 15)); // Same chunk

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

static void testChunkLoadProgressIncludesMeshingHalo() {
    client::ClientWorld world;
    world.setRenderDistance(1);

    for (int cx = -2; cx <= 2; ++cx) {
        for (int cz = -2; cz <= 2; ++cz) {
            if (cx * cx + cz * cz <= 4) {
                world.addChunk(std::make_shared<Chunk>(cx, cz));
            }
        }
    }

    const client::ClientWorld::ChunkLoadProgress progress = world.getChunkLoadProgress(glm::vec3(0.0f));
    require(progress.target == 13, "client load target should include the one-chunk meshing halo");
    require(progress.loaded == progress.target, "client load progress should count every loaded halo chunk");
    std::printf("[PASS] testChunkLoadProgressIncludesMeshingHalo\n");
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

static void testNeighborLinkingDirtiesExistingBorders() {
    client::ClientWorld cw;
    auto left = std::make_shared<Chunk>(0, 0);
    auto right = std::make_shared<Chunk>(1, 0);

    left->setBlock(15, 64, 0, defaultState("minecraft:stone"));
    right->setBlock(0, 64, 0, defaultState("minecraft:stone"));
    left->markMeshClean();
    right->markMeshClean();

    cw.addChunk(left);
    require(!left->isSubChunkDirty(Chunk::toSubChunkIndex(64)), "left chunk should start clean");
    cw.addChunk(right);

    const int scy = Chunk::toSubChunkIndex(64);
    require(left->neighbors[0] == right.get(), "left chunk should link +X neighbor");
    require(right->neighbors[1] == left.get(), "right chunk should link -X neighbor");
    require(left->getSubChunk(scy)->neighbors[0] == right->getSubChunk(scy), "left subchunk should link +X neighbor");
    require(right->getSubChunk(scy)->neighbors[1] == left->getSubChunk(scy), "right subchunk should link -X neighbor");
    require(left->isSubChunkDirty(scy), "left border subchunk should be dirtied");
    require(right->isSubChunkDirty(scy), "right border subchunk should be dirtied");

    cw.removeChunk(1, 0);
    require(left->neighbors[0] == nullptr, "left chunk should unlink removed +X neighbor");
    require(left->getSubChunk(scy)->neighbors[0] == nullptr, "left subchunk should unlink removed +X neighbor");
    require(left->isSubChunkDirty(scy), "left border subchunk should be dirtied after unlink");
    std::printf("[PASS] testNeighborLinkingDirtiesExistingBorders\n");
}

static void testLightOnlyUpdateDirtiesChangedNeighborBorders() {
    client::ClientWorld cw;
    auto left = std::make_shared<Chunk>(0, 0);
    auto right = std::make_shared<Chunk>(1, 0);
    left->setBlock(15, 64, 0, defaultState("minecraft:stone"));
    right->setBlock(0, 64, 0, defaultState("minecraft:stone"));
    cw.addChunk(left);
    cw.addChunk(right);

    constexpr int y = 64;
    const int scy = Chunk::toSubChunkIndex(y);
    left->markMeshClean();
    right->markMeshClean();

    std::vector<uint8_t> lightPatch(SubChunk::BLOCK_COUNT, 0);
    lightPatch[SubChunk::toIndex(0, Chunk::toSubChunkLocalY(y), 0)] = 15;
    cw.applyBlockUpdate(16, y, 0, net::BlockUpdateKind::LightOnly, NULL_BLOCK_STATE, lightPatch);

    require(right->isSubChunkDirty(scy), "light-only update should dirty the receiving chunk");
    require(left->isSubChunkDirty(scy), "light-only border update should dirty the adjacent chunk");

    left->markMeshClean();
    right->markMeshClean();
    std::fill(lightPatch.begin(), lightPatch.end(), 0);
    cw.applyBlockUpdate(16, y, 0, net::BlockUpdateKind::LightOnly, NULL_BLOCK_STATE, lightPatch);
    require(right->isSubChunkDirty(scy), "light-only removal should dirty the receiving chunk");
    require(left->isSubChunkDirty(scy), "light-only removal should dirty the adjacent chunk");
    std::printf("[PASS] testLightOnlyUpdateDirtiesChangedNeighborBorders\n");
}

static void testClientWorldRaycastHitsBlocks() {
    client::ClientWorld cw;
    auto chunk = std::make_shared<Chunk>(0, 0);
    chunk->setBlock(3, 64, 0, defaultState("minecraft:stone"));
    cw.addChunk(chunk);

    const PhysicsInfo ray(glm::vec3(0.5f, 64.5f, 0.5f), glm::vec3(1.0f, 0.0f, 0.0f));
    const RayHit hit = raycastWorldView(cw, ray, 8.0f);

    require(hit.hit, "client world raycast should hit block");
    require(hit.blockPos == glm::ivec3(3, 64, 0), "client world raycast should return block position");
    std::printf("[PASS] testClientWorldRaycastHitsBlocks\n");
}

static void testClientWorldRaycastSeparatesFluidAndBlockTargets() {
    client::ClientWorld cw;
    auto chunk = std::make_shared<Chunk>(0, 0);
    chunk->setBlock(1, 64, 0, FluidState::makeWater(0, false));
    chunk->setBlock(3, 64, 0, defaultState("minecraft:stone"));
    cw.addChunk(chunk);

    const PhysicsInfo ray(glm::vec3(0.5f, 64.5f, 0.5f), glm::vec3(1.0f, 0.0f, 0.0f));
    const RayHit blockHit = raycastWorldView(cw, ray, 8.0f);
    require(blockHit.hit, "default raycast should hit the block behind water");
    require(blockHit.kind == RayHitKind::Block, "default raycast should report a block hit");
    require(blockHit.blockPos == glm::ivec3(3, 64, 0), "default raycast should ignore water cells");

    const RayHit fluidHit = raycastWorldView(cw, ray, 8.0f, RaycastFluidMode::Include);
    require(fluidHit.hit, "fluid raycast should hit water");
    require(fluidHit.kind == RayHitKind::Fluid, "fluid raycast should report a fluid hit");
    require(fluidHit.blockPos == glm::ivec3(1, 64, 0), "fluid raycast should return the water position");
    require(FluidState::isSource(cw.getFluidState(1, 64, 0)), "client world should expose block-layer water as fluid");
    std::printf("[PASS] testClientWorldRaycastSeparatesFluidAndBlockTargets\n");
}

static void testApplyBlockUpdateAcceptsVariableLightPatch() {
    client::ClientWorld cw;
    auto chunk = std::make_shared<Chunk>(0, 0);
    cw.addChunk(chunk);

    constexpr int patchSide = 5;
    constexpr int patchRadius = patchSide / 2;
    std::vector<uint8_t> lightPatch;
    lightPatch.reserve(patchSide * patchSide * patchSide);
    for (int dy = -patchRadius; dy <= patchRadius; ++dy) {
        for (int dz = -patchRadius; dz <= patchRadius; ++dz) {
            for (int dx = -patchRadius; dx <= patchRadius; ++dx) {
                lightPatch.push_back(static_cast<uint8_t>((dx == 2 && dy == 0 && dz == 0) ? 0x0C : 0));
            }
        }
    }

    cw.applyBlockUpdate(4, 64, 4, defaultState("minecraft:torch"), lightPatch);

    require(cw.getBlockState(4, 64, 4) == defaultState("minecraft:torch"),
            "block update should still apply the block state");
    require(cw.getBlockContentRevision() == 3, "block updates should bump block content revision");
    require(cw.getPackedLight(6, 64, 4) == 0x0C, "client should apply larger odd-cube light patches");
    std::printf("[PASS] testApplyBlockUpdateAcceptsVariableLightPatch\n");
}

static void testApplyBlockUpdatePreservesStateId() {
    client::ClientWorld cw;
    auto chunk = std::make_shared<Chunk>(0, 0);
    cw.addChunk(chunk);

    const BlockStateId poweredTarget = targetState(PropIndices::POWER_9);
    cw.applyBlockUpdate(5, 64, 5, poweredTarget);

    require(cw.getBlockState(5, 64, 5) == poweredTarget,
            "client block updates should preserve non-default block state ids");
    require(BlockStateRegistry::getBlockId(cw.getBlockState(5, 64, 5)) ==
                BlockRegistry::requireIdByName("minecraft:target"),
            "client block updates should keep the state mapped to its block id");
    std::printf("[PASS] testApplyBlockUpdatePreservesStateId\n");
}

static void testApplyBlockUpdateAcceptsLightSection() {
    client::ClientWorld cw;
    auto chunk = std::make_shared<Chunk>(0, 0);
    cw.addChunk(chunk);

    std::vector<uint8_t> sectionLight(SubChunk::BLOCK_COUNT, 0);
    const int localIndex = 4 + 4 * SubChunk::SIZE + 4 * SubChunk::SIZE * SubChunk::SIZE;
    sectionLight[localIndex] = 0x0B;

    cw.applyBlockUpdate(0, 64, 0, net::BlockUpdateKind::LightOnly, NULL_BLOCK_STATE, sectionLight);

    require(cw.getPackedLight(4, 68, 4) == 0x0B, "client should apply subchunk light section updates");
    require(chunk->getLightRevision() > 0, "subchunk light section updates should bump light revision");
    require(chunk->isSubChunkDirty(Chunk::toSubChunkIndex(68)),
            "subchunk light section updates should dirty terrain mesh");
    std::printf("[PASS] testApplyBlockUpdateAcceptsLightSection\n");
}

static void testApplyBlockUpdateAcceptsFullChunkLightSnapshot() {
    client::ClientWorld cw;
    auto chunk = std::make_shared<Chunk>(0, 0);
    cw.addChunk(chunk);

    std::vector<uint8_t> fullLight(Chunk::BLOCK_COUNT, 0);
    fullLight[Chunk::toIndex(15, 127, 15)] = 0xF0;

    cw.applyBlockUpdate(4, 64, 4, defaultState("minecraft:stone"), fullLight);

    require(cw.getBlockState(4, 64, 4) == defaultState("minecraft:stone"),
            "full light snapshot update should still apply the block state");
    require(cw.getPackedLight(15, 127, 15) == 0xF0, "client should apply full chunk light snapshots");
    std::printf("[PASS] testApplyBlockUpdateAcceptsFullChunkLightSnapshot\n");
}

static void testApplyBlockUpdateCanBeLightOnly() {
    client::ClientWorld cw;
    auto chunk = std::make_shared<Chunk>(0, 0);
    chunk->setBlock(4, 64, 4, defaultState("minecraft:stone"));
    cw.addChunk(chunk);

    std::vector<uint8_t> fullLight(Chunk::BLOCK_COUNT, 0);
    fullLight[Chunk::toIndex(4, 64, 4)] = 0x0A;

    const uint64_t beforeRevision = cw.getBlockContentRevision();
    cw.applyBlockUpdate(0, 0, 0, net::BlockUpdateKind::LightOnly, NULL_BLOCK_STATE, fullLight);

    require(cw.getBlockState(4, 64, 4) == defaultState("minecraft:stone"), "light-only updates should not edit blocks");
    require(cw.getBlockContentRevision() == beforeRevision,
            "light-only updates should not bump block content revision");
    require(cw.getPackedLight(4, 64, 4) == 0x0A, "light-only updates should apply packed light");
    std::printf("[PASS] testApplyBlockUpdateCanBeLightOnly\n");
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    BlockRegistry::init(nullptr);

    testInitialState();
    testAddChunk();
    testRemoveChunk();
    testRemoveNonexistentChunk();
    testGetBlockFromClientWorld();
    testIsChunkLoadedForBlock();
    testGetChunkCoords();
    testSetRenderDistance();
    testChunkLoadProgressIncludesMeshingHalo();
    testWeatherDayNightProxy();
    testNeighborLinkingDirtiesExistingBorders();
    testLightOnlyUpdateDirtiesChangedNeighborBorders();
    testClientWorldRaycastHitsBlocks();
    testClientWorldRaycastSeparatesFluidAndBlockTargets();
    testApplyBlockUpdateAcceptsVariableLightPatch();
    testApplyBlockUpdatePreservesStateId();
    testApplyBlockUpdateAcceptsLightSection();
    testApplyBlockUpdateAcceptsFullChunkLightSnapshot();
    testApplyBlockUpdateCanBeLightOnly();
    std::printf("\nAll ClientWorld tests passed!\n");
    return 0;
}
