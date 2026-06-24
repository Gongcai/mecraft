// Unit tests for the save system: ChunkSerializer round-trip, SavePaths, SaveManager, PlayerSerializer.

#include "save/ChunkSerializer.h"
#include "save/SavePaths.h"
#include "save/SaveManager.h"
#include "save/SaveFormat.h"
#include "save/PlayerSerializer.h"
#include "world/chunk/Chunk.h"
#include "world/block/Block.h"
#include "world/block/BlockStateRegistry.h"
#include "world/block/PropIndices.h"
#include "item/Item.h"

#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// ChunkSerializer round-trip tests
// ---------------------------------------------------------------------------

namespace {

void appendU8(std::vector<uint8_t>& out, const uint8_t value) {
    out.push_back(value);
}

void appendU16(std::vector<uint8_t>& out, const uint16_t value) {
    out.push_back(static_cast<uint8_t>(value));
    out.push_back(static_cast<uint8_t>(value >> 8));
}

void appendU32(std::vector<uint8_t>& out, const uint32_t value) {
    out.push_back(static_cast<uint8_t>(value));
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value >> 16));
    out.push_back(static_cast<uint8_t>(value >> 24));
}

void appendVaruint(std::vector<uint8_t>& out, uint32_t value) {
    while (value >= 0x80u) {
        out.push_back(static_cast<uint8_t>((value & 0x7Fu) | 0x80u));
        value >>= 7;
    }
    out.push_back(static_cast<uint8_t>(value));
}

void appendString(std::vector<uint8_t>& out, const std::string& value) {
    appendVaruint(out, static_cast<uint32_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
}

std::vector<uint8_t> buildSinglePaletteStateFile(const int32_t cx,
                                                 const int32_t cz,
                                                 const uint8_t scy,
                                                 const std::string& stateName,
                                                 const uint8_t bitsPerEntry,
                                                 const std::vector<uint8_t>& packedData) {
    std::vector<uint8_t> payload;
    appendU8(payload, save::MCHK_ENCODING_PALLETIZED);
    appendU16(payload, static_cast<uint16_t>(1u << scy));
    appendU8(payload, scy);

    appendVaruint(payload, 1);
    appendString(payload, stateName);
    appendU8(payload, bitsPerEntry);
    appendU32(payload, static_cast<uint32_t>(packedData.size()));
    payload.insert(payload.end(), packedData.begin(), packedData.end());

    appendVaruint(payload, 0);

    save::MchkHeader header{};
    header.magic = save::MCHK_MAGIC;
    header.version = save::MCHK_VERSION;
    header.flags = 0;
    header.chunkX = cx;
    header.chunkZ = cz;
    header.payloadSize = static_cast<uint32_t>(payload.size());
    header.payloadCrc32 = save::detail::crc32(payload.data(), payload.size());

    std::vector<uint8_t> file(sizeof(save::MchkHeader));
    std::memcpy(file.data(), &header, sizeof(header));
    file.insert(file.end(), payload.begin(), payload.end());
    return file;
}

} // namespace

static void testEmptyChunkRoundTrip() {
    auto original = std::make_shared<Chunk>(0, 0);

    std::vector<uint8_t> fileData = save::ChunkSerializer::serializeFile(*original);
    assert(!fileData.empty());

    auto loaded = save::ChunkSerializer::deserializeFile(fileData.data(), fileData.size());
    assert(loaded != nullptr);
    assert(loaded->m_chunkX == 0);
    assert(loaded->m_chunkZ == 0);

    // All blocks should be AIR
    for (int y = 0; y < Chunk::SIZE_Y; ++y) {
        for (int z = 0; z < Chunk::SIZE_Z; ++z) {
            for (int x = 0; x < Chunk::SIZE_X; ++x) {
                assert(loaded->getBlock(x, y, z) == BlockIds::AIR);
            }
        }
    }
    std::printf("[PASS] testEmptyChunkRoundTrip\n");
}

static void testSingleSubchunkRoundTrip() {
    auto original = std::make_shared<Chunk>(0, 0);

    // Fill subchunk 4 (y=64..79) with stone
    for (int y = 64; y < 80; ++y) {
        for (int z = 0; z < Chunk::SIZE_Z; ++z) {
            for (int x = 0; x < Chunk::SIZE_X; ++x) {
                original->setBlockFast(x, y, z, BlockIds::STONE);
            }
        }
    }

    std::vector<uint8_t> fileData = save::ChunkSerializer::serializeFile(*original);
    auto loaded = save::ChunkSerializer::deserializeFile(fileData.data(), fileData.size());
    assert(loaded != nullptr);

    // Verify stone in subchunk 4
    for (int y = 64; y < 80; ++y) {
        for (int z = 0; z < Chunk::SIZE_Z; ++z) {
            for (int x = 0; x < Chunk::SIZE_X; ++x) {
                assert(loaded->getBlock(x, y, z) == BlockIds::STONE);
            }
        }
    }

    // Verify air elsewhere
    for (int y = 0; y < 64; ++y) {
        assert(loaded->getBlock(0, y, 0) == BlockIds::AIR);
    }
    for (int y = 80; y < Chunk::SIZE_Y; ++y) {
        assert(loaded->getBlock(0, y, 0) == BlockIds::AIR);
    }
    std::printf("[PASS] testSingleSubchunkRoundTrip\n");
}

static void testMultipleSubchunksRoundTrip() {
    auto original = std::make_shared<Chunk>(0, 0);

    // Fill subchunk 0 (y=0..15) with dirt
    for (int y = 0; y < 16; ++y) {
        for (int z = 0; z < Chunk::SIZE_Z; ++z) {
            for (int x = 0; x < Chunk::SIZE_X; ++x) {
                original->setBlockFast(x, y, z, BlockIds::DIRT);
            }
        }
    }

    // Fill subchunk 4 (y=64..79) with stone
    for (int y = 64; y < 80; ++y) {
        for (int z = 0; z < Chunk::SIZE_Z; ++z) {
            for (int x = 0; x < Chunk::SIZE_X; ++x) {
                original->setBlockFast(x, y, z, BlockIds::STONE);
            }
        }
    }

    // Fill subchunk 7 (y=112..127) with grass
    for (int y = 112; y < 128; ++y) {
        for (int z = 0; z < Chunk::SIZE_Z; ++z) {
            for (int x = 0; x < Chunk::SIZE_X; ++x) {
                original->setBlockFast(x, y, z, BlockIds::GRASS);
            }
        }
    }

    std::vector<uint8_t> fileData = save::ChunkSerializer::serializeFile(*original);
    auto loaded = save::ChunkSerializer::deserializeFile(fileData.data(), fileData.size());
    assert(loaded != nullptr);

    // Verify each subchunk
    for (int y = 0; y < 16; ++y) {
        assert(loaded->getBlock(5, y, 5) == BlockIds::DIRT);
    }
    for (int y = 64; y < 80; ++y) {
        assert(loaded->getBlock(5, y, 5) == BlockIds::STONE);
    }
    for (int y = 112; y < 128; ++y) {
        assert(loaded->getBlock(5, y, 5) == BlockIds::GRASS);
    }

    // Verify air between subchunks
    assert(loaded->getBlock(5, 32, 5) == BlockIds::AIR);
    assert(loaded->getBlock(5, 96, 5) == BlockIds::AIR);
    std::printf("[PASS] testMultipleSubchunksRoundTrip\n");
}

static void testNegativeCoordinatesRoundTrip() {
    auto original = std::make_shared<Chunk>(-3, 5);

    for (int y = 64; y < 80; ++y) {
        for (int z = 0; z < Chunk::SIZE_Z; ++z) {
            for (int x = 0; x < Chunk::SIZE_X; ++x) {
                original->setBlockFast(x, y, z, BlockIds::STONE);
            }
        }
    }

    std::vector<uint8_t> fileData = save::ChunkSerializer::serializeFile(*original);
    auto loaded = save::ChunkSerializer::deserializeFile(fileData.data(), fileData.size());
    assert(loaded != nullptr);
    assert(loaded->m_chunkX == -3);
    assert(loaded->m_chunkZ == 5);

    for (int y = 64; y < 80; ++y) {
        assert(loaded->getBlock(8, y, 8) == BlockIds::STONE);
    }
    std::printf("[PASS] testNegativeCoordinatesRoundTrip\n");
}

static void testMixedBlocksInSubchunk() {
    auto original = std::make_shared<Chunk>(0, 0);

    // Mix of different blocks in subchunk 4
    for (int x = 0; x < Chunk::SIZE_X; ++x) {
        original->setBlockFast(x, 64, 0, BlockIds::STONE);
        original->setBlockFast(x, 65, 0, BlockIds::DIRT);
        original->setBlockFast(x, 66, 0, BlockIds::GRASS);
        original->setBlockFast(x, 67, 0, BlockIds::STONE);
        original->setBlockFast(x, 68, 0, BlockIds::OAK_PLANKS);
    }

    std::vector<uint8_t> fileData = save::ChunkSerializer::serializeFile(*original);
    auto loaded = save::ChunkSerializer::deserializeFile(fileData.data(), fileData.size());
    assert(loaded != nullptr);

    for (int x = 0; x < Chunk::SIZE_X; ++x) {
        assert(loaded->getBlock(x, 64, 0) == BlockIds::STONE);
        assert(loaded->getBlock(x, 65, 0) == BlockIds::DIRT);
        assert(loaded->getBlock(x, 66, 0) == BlockIds::GRASS);
        assert(loaded->getBlock(x, 67, 0) == BlockIds::STONE);
        assert(loaded->getBlock(x, 68, 0) == BlockIds::OAK_PLANKS);
    }
    std::printf("[PASS] testMixedBlocksInSubchunk\n");
}

static void testBlockStateRoundTrip() {
    auto original = std::make_shared<Chunk>(0, 0);

    const BlockID oakStairs = BlockRegistry::findByName("oak_stairs");
    if (oakStairs == BlockIds::AIR) {
        std::fprintf(stderr, "[FAIL] oak_stairs should be registered\n");
        std::abort();
    }
    const StateID northBottomStairs = BlockStateRegistry::getState(
        oakStairs,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, PropIndices::FACING_NORTH},
            {PropIndices::HALF, PropIndices::HALF_BOTTOM},
            {PropIndices::SHAPE, PropIndices::SHAPE_OUTER_LEFT}
        });
    original->setBlock(4, 64, 4, northBottomStairs);

    std::vector<uint8_t> fileData = save::ChunkSerializer::serializeFile(*original);
    auto loaded = save::ChunkSerializer::deserializeFile(fileData.data(), fileData.size());
    if (loaded == nullptr) {
        std::fprintf(stderr, "[FAIL] chunk with state ids should deserialize\n");
        std::abort();
    }

    const StateID loadedState = loaded->getBlock(4, 64, 4);
    if (loadedState != northBottomStairs ||
        BlockStateRegistry::getPropertyIndex(loadedState, PropIndices::FACING) != PropIndices::FACING_NORTH ||
        BlockStateRegistry::getPropertyIndex(loadedState, PropIndices::HALF) != PropIndices::HALF_BOTTOM ||
        BlockStateRegistry::getPropertyIndex(loadedState, PropIndices::SHAPE) != PropIndices::SHAPE_OUTER_LEFT) {
        std::fprintf(stderr,
                     "[FAIL] chunk serializer should preserve full stair state: expected=%s loaded=%s\n",
                     BlockStateRegistry::stateToString(northBottomStairs).c_str(),
                     BlockStateRegistry::stateToString(loadedState).c_str());
        std::abort();
    }
    std::printf("[PASS] testBlockStateRoundTrip\n");
}

static void testChestBlockStateRoundTrip() {
    auto original = std::make_shared<Chunk>(0, 0);

    const StateID eastChest = BlockStateRegistry::getState(
        BlockIds::CHEST,
        PropIndices::FACING,
        PropIndices::FACING_EAST);
    original->setBlock(6, 70, 5, eastChest);

    const std::vector<uint8_t> fileData = save::ChunkSerializer::serializeFile(*original);
    auto loaded = save::ChunkSerializer::deserializeFile(fileData.data(), fileData.size());
    if (loaded == nullptr) {
        std::fprintf(stderr, "[FAIL] chunk with chest state should deserialize\n");
        std::abort();
    }

    const StateID loadedState = loaded->getBlock(6, 70, 5);
    if (BlockStateRegistry::getBlockId(loadedState) != BlockIds::CHEST ||
        BlockStateRegistry::getPropertyIndex(loadedState, PropIndices::FACING) != PropIndices::FACING_EAST) {
        std::fprintf(stderr,
                     "[FAIL] chunk serializer should preserve chest state: expected=%s loaded=%s\n",
                     BlockStateRegistry::stateToString(eastChest).c_str(),
                     BlockStateRegistry::stateToString(loadedState).c_str());
        std::abort();
    }

    std::printf("[PASS] testChestBlockStateRoundTrip\n");
}

static void testBareStateNameLoadsDefaultBlockState() {
    const BlockID oakStairs = BlockRegistry::findByName("oak_stairs");
    if (oakStairs == BlockIds::AIR) {
        std::fprintf(stderr, "[FAIL] oak_stairs should be registered\n");
        std::abort();
    }

    const std::vector<uint8_t> fileData = buildSinglePaletteStateFile(
        0,
        0,
        4,
        "minecraft:oak_stairs",
        0,
        {});
    auto loaded = save::ChunkSerializer::deserializeFile(fileData.data(), fileData.size());
    if (loaded == nullptr) {
        std::fprintf(stderr, "[FAIL] bare block state names should deserialize\n");
        std::abort();
    }

    const StateID expectedDefault = BlockStateRegistry::getDefaultState(oakStairs);
    const StateID loadedState = loaded->getBlock(0, 64, 0);
    if (loadedState != expectedDefault ||
        loadedState == oakStairs ||
        BlockStateRegistry::getModelVariant(loadedState) == nullptr) {
        std::fprintf(stderr,
                     "[FAIL] bare state name should resolve to default block state: expected=%s loaded=%s\n",
                     BlockStateRegistry::stateToString(expectedDefault).c_str(),
                     BlockStateRegistry::stateToString(loadedState).c_str());
        std::abort();
    }
    std::printf("[PASS] testBareStateNameLoadsDefaultBlockState\n");
}

static void testInvalidPackedPaletteIndexRejected() {
    const std::vector<uint8_t> packedData(512, 0xFFu);
    const std::vector<uint8_t> fileData = buildSinglePaletteStateFile(
        0,
        0,
        4,
        "minecraft:stone",
        1,
        packedData);

    auto loaded = save::ChunkSerializer::deserializeFile(fileData.data(), fileData.size());
    if (loaded != nullptr) {
        std::fprintf(stderr, "[FAIL] packed palette indices outside the palette should be rejected\n");
        std::abort();
    }
    std::printf("[PASS] testInvalidPackedPaletteIndexRejected\n");
}

static void testFluidLayerRoundTrip() {
    auto original = std::make_shared<Chunk>(0, 0);

    // Place a stone block with water on top (waterlogged scenario)
    original->setBlockFast(5, 64, 5, BlockIds::STONE);

    // Set fluid layer at (5, 65, 5) to water
    SubChunk* sub = original->getSubChunk(Chunk::toSubChunkIndex(65));
    if (!sub) {
        sub = original->getOrCreateSubChunk(Chunk::toSubChunkIndex(65));
    }
    sub->setFluidLayer(5, Chunk::toSubChunkLocalY(65), 5, BlockIds::WATER);

    std::vector<uint8_t> fileData = save::ChunkSerializer::serializeFile(*original);
    auto loaded = save::ChunkSerializer::deserializeFile(fileData.data(), fileData.size());
    assert(loaded != nullptr);

    assert(loaded->getBlock(5, 64, 5) == BlockIds::STONE);

    const SubChunk* loadedSub = loaded->getSubChunk(Chunk::toSubChunkIndex(65));
    assert(loadedSub != nullptr);
    assert(loadedSub->getFluidLayer(5, Chunk::toSubChunkLocalY(65), 5) == BlockIds::WATER);
    std::printf("[PASS] testFluidLayerRoundTrip\n");
}

static void testPayloadSizeReasonable() {
    auto original = std::make_shared<Chunk>(0, 0);

    // A single block should produce a small payload
    original->setBlockFast(0, 64, 0, BlockIds::STONE);

    std::vector<uint8_t> fileData = save::ChunkSerializer::serializeFile(*original);
    // Header is 24 bytes, payload should be modest for a mostly-empty chunk.
    assert(fileData.size() < 500);
    std::printf("[PASS] testPayloadSizeReasonable\n");
}

// ---------------------------------------------------------------------------
// SavePaths tests
// ---------------------------------------------------------------------------

static void testSavePathsSanitizeWorldName() {
    assert(save::SavePaths::sanitizeWorldName("My World") == "My World");
    assert(save::SavePaths::sanitizeWorldName("My:World") == "My_World");
    assert(save::SavePaths::sanitizeWorldName(R"(My\World/Name)") == "My_World_Name");
    assert(save::SavePaths::sanitizeWorldName("  trimmed  ") == "trimmed");
    assert(save::SavePaths::sanitizeWorldName("") == "New World");
    assert(save::SavePaths::sanitizeWorldName("???") == "New World");
    std::printf("[PASS] testSavePathsSanitizeWorldName\n");
}

static void testSavePathsChunkPath() {
    save::SavePaths paths("saves/TestWorld");

    assert(paths.chunkPath(0, 0).filename() == "c.0.0.mchk");
    assert(paths.chunkPath(-1, 3).filename() == "c.-1.3.mchk");
    assert(paths.chunkPath(100, -200).filename() == "c.100.-200.mchk");

    assert(paths.chunkTmpPath(0, 0).filename() == "c.0.0.mchk.tmp");
    assert(paths.chunkBakPath(0, 0).filename() == "c.0.0.mchk.bak");
    std::printf("[PASS] testSavePathsChunkPath\n");
}

static void testSavePathsEnsureDirectories() {
    const std::string testRoot = "test_save_paths_dir";
    save::SavePaths paths(testRoot);

    paths.ensureDirectories();
    assert(std::filesystem::exists(paths.chunksDir()));
    assert(std::filesystem::exists(paths.blockEntitiesDir()));
    assert(std::filesystem::exists(paths.playersDir()));

    // Cleanup
    std::filesystem::remove_all(testRoot);
    std::printf("[PASS] testSavePathsEnsureDirectories\n");
}

// ---------------------------------------------------------------------------
// SaveManager integration test
// ---------------------------------------------------------------------------

static void testSaveManagerLevelMeta() {
    const std::string testRoot = "test_save_manager_meta";
    save::SaveManager mgr(testRoot);
    mgr.paths().ensureDirectories();

    // Save level meta with full data
    save::LevelMeta meta;
    meta.seed = 42;
    meta.displayName = "Custom World";
    meta.spawnX = 1.0f;
    meta.spawnY = 68.0f;
    meta.spawnZ = 2.0f;
    meta.timeOfDay = 600.0f;
    meta.totalGameTime = 12000.0;
    meta.elapsedDays = 1;
    meta.weatherType = "rain";
    meta.weatherWetness = 0.5f;
    meta.weatherStorm = 0.0f;
    mgr.saveLevelMeta(meta);

    // Load it back
    save::LevelMeta loaded;
    assert(mgr.loadLevelMeta(loaded));
    assert(loaded.seed == 42);
    assert(loaded.displayName == "Custom World");
    assert(loaded.spawnX == 1.0f);
    assert(loaded.spawnY == 68.0f);
    assert(loaded.timeOfDay == 600.0f);
    assert(loaded.elapsedDays == 1);
    assert(loaded.weatherType == "rain");
    assert(loaded.weatherWetness == 0.5f);

    // Cleanup
    std::filesystem::remove_all(testRoot);
    std::printf("[PASS] testSaveManagerLevelMeta\n");
}

static void testSaveManagerChunkRoundTrip() {
    const std::string testRoot = "test_save_manager_chunk";
    save::SaveManager mgr(testRoot);
    mgr.paths().ensureDirectories();

    // Create and save a chunk
    {
        auto chunk = std::make_shared<Chunk>(7, -2);
        for (int y = 64; y < 80; ++y) {
            for (int z = 0; z < Chunk::SIZE_Z; ++z) {
                for (int x = 0; x < Chunk::SIZE_X; ++x) {
                    chunk->setBlockFast(x, y, z, BlockIds::WOOD);
                }
            }
        }
        mgr.submitSaveChunk(7, -2, *chunk);
        mgr.flushPendingSaves();
    }

    // Load it back
    assert(mgr.chunkFileExists(7, -2));
    auto loaded = mgr.tryLoadChunk(7, -2);
    assert(loaded != nullptr);
    assert(loaded->m_chunkX == 7);
    assert(loaded->m_chunkZ == -2);

    for (int y = 64; y < 80; ++y) {
        assert(loaded->getBlock(8, y, 8) == BlockIds::WOOD);
    }

    // Cleanup
    std::filesystem::remove_all(testRoot);
    std::printf("[PASS] testSaveManagerChunkRoundTrip\n");
}

static void testSaveManagerNonexistentChunk() {
    const std::string testRoot = "test_save_manager_noexist";
    save::SaveManager mgr(testRoot);
    mgr.paths().ensureDirectories();

    assert(!mgr.chunkFileExists(0, 0));
    assert(mgr.tryLoadChunk(0, 0) == nullptr);

    // Cleanup
    std::filesystem::remove_all(testRoot);
    std::printf("[PASS] testSaveManagerNonexistentChunk\n");
}

static void testSaveManagerPersistentEntitiesRoundTrip() {
    const std::string testRoot = "test_save_manager_entities";
    save::SaveManager mgr(testRoot);
    mgr.paths().ensureDirectories();

    save::PersistentEntityData zombie;
    zombie.type = "minecraft:zombie";
    zombie.posX = 3.5f;
    zombie.posY = 64.0f;
    zombie.posZ = -7.25f;
    zombie.velX = 0.1f;
    zombie.velY = -0.2f;
    zombie.velZ = 0.3f;
    zombie.yaw = 45.0f;
    zombie.health = 12;
    zombie.healthMax = 20;

    save::PersistentEntityData drop;
    drop.type = "minecraft:item";
    drop.posX = 8.25f;
    drop.posY = 65.5f;
    drop.posZ = -3.0f;
    drop.velX = 0.4f;
    drop.velY = 0.5f;
    drop.velZ = -0.6f;
    drop.yaw = 1.25f;
    drop.spinSpeed = 2.75f;
    drop.itemId = ItemIds::COAL;
    drop.stackCount = 4;
    drop.dropId = 42;
    drop.halfExtentX = 0.2f;
    drop.halfExtentY = 0.21f;
    drop.halfExtentZ = 0.22f;
    drop.ageSeconds = 3.5f;
    drop.lifeTimeSeconds = 27.0f;
    drop.grounded = true;

    mgr.savePersistentEntities({zombie, drop});

    std::vector<save::PersistentEntityData> loaded;
    assert(mgr.loadPersistentEntities(loaded));
    assert(loaded.size() == 2);

    const save::PersistentEntityData* loadedZombie = nullptr;
    const save::PersistentEntityData* loadedDrop = nullptr;
    for (const auto& entity : loaded) {
        if (entity.type == "minecraft:zombie") {
            loadedZombie = &entity;
        } else if (entity.type == "minecraft:item") {
            loadedDrop = &entity;
        }
    }

    assert(loadedZombie != nullptr);
    assert(loadedZombie->posX == zombie.posX);
    assert(loadedZombie->posZ == zombie.posZ);
    assert(loadedZombie->velZ == zombie.velZ);
    assert(loadedZombie->yaw == zombie.yaw);
    assert(loadedZombie->health == zombie.health);

    assert(loadedDrop != nullptr);
    assert(loadedDrop->itemId == drop.itemId);
    assert(loadedDrop->stackCount == drop.stackCount);
    assert(loadedDrop->dropId == drop.dropId);
    assert(loadedDrop->halfExtentY == drop.halfExtentY);
    assert(loadedDrop->spinSpeed == drop.spinSpeed);
    assert(loadedDrop->ageSeconds == drop.ageSeconds);
    assert(loadedDrop->lifeTimeSeconds == drop.lifeTimeSeconds);
    assert(loadedDrop->grounded == drop.grounded);

    std::filesystem::remove_all(testRoot);
    std::printf("[PASS] testSaveManagerPersistentEntitiesRoundTrip\n");
}

static void testSaveManagerBlockEntitiesRoundTrip() {
    const std::string testRoot = "test_save_manager_block_entities";
    save::SaveManager mgr(testRoot);
    mgr.paths().ensureDirectories();

    save::BlockEntityData chest;
    chest.type = "minecraft:chest";
    chest.x = -4;
    chest.y = 72;
    chest.z = 9;

    save::BlockEntitySlotData apple;
    apple.slot = 0;
    apple.itemId = ItemIds::APPLE;
    apple.count = 5;
    chest.slots.push_back(apple);

    save::BlockEntitySlotData pickaxe;
    pickaxe.slot = 17;
    pickaxe.itemId = ItemIds::IRON_PICKAXE;
    pickaxe.count = 1;
    pickaxe.durability = 93;
    chest.slots.push_back(pickaxe);

    save::BlockEntityData emptyChest;
    emptyChest.type = "minecraft:chest";
    emptyChest.x = 1;
    emptyChest.y = 2;
    emptyChest.z = 3;

    save::BlockEntityData furnace;
    furnace.type = "minecraft:furnace";
    furnace.x = 8;
    furnace.y = 65;
    furnace.z = -2;
    furnace.burnSecondsRemaining = 3.5f;
    furnace.burnSecondsTotal = 8.0f;
    furnace.cookSeconds = 6.25f;
    furnace.cookTargetSeconds = 10.0f;

    save::BlockEntitySlotData fuel;
    fuel.slot = 1;
    fuel.itemId = ItemIds::COAL;
    fuel.count = 2;
    furnace.slots.push_back(fuel);

    mgr.saveBlockEntities({chest, emptyChest, furnace});

    std::vector<save::BlockEntityData> loaded;
    assert(mgr.loadBlockEntities(loaded));
    assert(loaded.size() == 3);
    assert(loaded[0].type == "minecraft:chest");
    assert(loaded[0].x == chest.x);
    assert(loaded[0].y == chest.y);
    assert(loaded[0].z == chest.z);
    assert(loaded[0].slots.size() == 2);
    assert(loaded[0].slots[0].slot == apple.slot);
    assert(loaded[0].slots[0].itemId == apple.itemId);
    assert(loaded[0].slots[0].count == apple.count);
    assert(loaded[0].slots[1].slot == pickaxe.slot);
    assert(loaded[0].slots[1].itemId == pickaxe.itemId);
    assert(loaded[0].slots[1].durability == pickaxe.durability);
    assert(loaded[1].type == "minecraft:chest");
    assert(loaded[1].x == emptyChest.x);
    assert(loaded[1].y == emptyChest.y);
    assert(loaded[1].z == emptyChest.z);
    assert(loaded[1].slots.empty());
    assert(loaded[2].type == "minecraft:furnace");
    assert(loaded[2].x == furnace.x);
    assert(loaded[2].y == furnace.y);
    assert(loaded[2].z == furnace.z);
    assert(loaded[2].burnSecondsRemaining == furnace.burnSecondsRemaining);
    assert(loaded[2].burnSecondsTotal == furnace.burnSecondsTotal);
    assert(loaded[2].cookSeconds == furnace.cookSeconds);
    assert(loaded[2].cookTargetSeconds == furnace.cookTargetSeconds);
    assert(loaded[2].slots.size() == 1);
    assert(loaded[2].slots[0].slot == fuel.slot);
    assert(loaded[2].slots[0].itemId == fuel.itemId);
    assert(loaded[2].slots[0].count == fuel.count);

    mgr.saveBlockEntities({});
    loaded.clear();
    assert(mgr.loadBlockEntities(loaded));
    assert(loaded.empty());

    std::filesystem::remove_all(testRoot);
    std::printf("[PASS] testSaveManagerBlockEntitiesRoundTrip\n");
}

// ---------------------------------------------------------------------------
// PlayerSerializer tests
// ---------------------------------------------------------------------------

static void testPlayerSerializerRoundTrip() {
    save::PlayerData data;
    data.posX = 10.5f;
    data.posY = 65.0f;
    data.posZ = -3.2f;
    data.velX = 1.0f;
    data.velY = 0.0f;
    data.velZ = -0.5f;
    data.yaw = 45.0f;
    data.pitch = -15.0f;
    data.health = 15;
    data.healthMax = 20;
    data.armor = 5;
    data.armorMax = 20;
    data.food = 18;
    data.foodMax = 20;
    data.saturation = 3;
    data.isFlying = true;
    data.selectedSlot = 3;

    // Add some inventory items
    data.inventory.resize(36);
    data.inventory[0].item = "minecraft:stone";
    data.inventory[0].count = 64;
    data.inventory[5].item = "minecraft:iron_pickaxe";
    data.inventory[5].count = 1;
    data.inventory[5].durability = 100;

    // Serialize
    nlohmann::json j = save::PlayerSerializer::serialize(data);

    // Deserialize
    save::PlayerData loaded;
    assert(save::PlayerSerializer::deserialize(j, loaded));

    // Verify
    assert(loaded.posX == data.posX);
    assert(loaded.posY == data.posY);
    assert(loaded.posZ == data.posZ);
    assert(loaded.yaw == data.yaw);
    assert(loaded.pitch == data.pitch);
    assert(loaded.health == 15);
    assert(loaded.armor == 5);
    assert(loaded.food == 18);
    assert(loaded.saturation == 3);
    assert(loaded.isFlying == true);
    assert(loaded.selectedSlot == 3);
    assert(loaded.inventory.size() >= 6);
    assert(loaded.inventory[0].item == "minecraft:stone");
    assert(loaded.inventory[0].count == 64);
    assert(loaded.inventory[5].item == "minecraft:iron_pickaxe");
    assert(loaded.inventory[5].durability == 100);

    std::printf("[PASS] testPlayerSerializerRoundTrip\n");
}

static void testPlayerSerializerFileRoundTrip() {
    const std::string testFile = "test_player_save.json";

    save::PlayerData data;
    data.posX = 100.0f;
    data.posY = 64.0f;
    data.posZ = 200.0f;
    data.health = 20;
    data.food = 20;

    // Save to file
    save::PlayerSerializer::saveToFile(testFile, data);

    // Load from file
    save::PlayerData loaded;
    assert(save::PlayerSerializer::loadFromFile(testFile, loaded));
    assert(loaded.posX == 100.0f);
    assert(loaded.posY == 64.0f);
    assert(loaded.posZ == 200.0f);
    assert(loaded.health == 20);

    // Cleanup
    std::filesystem::remove(testFile);
    std::filesystem::remove(testFile + ".tmp");
    std::filesystem::remove(testFile + ".bak");
    std::printf("[PASS] testPlayerSerializerFileRoundTrip\n");
}

static void testPlayerSerializerNonexistentFile() {
    save::PlayerData loaded;
    assert(!save::PlayerSerializer::loadFromFile("nonexistent_player.json", loaded));
    std::printf("[PASS] testPlayerSerializerNonexistentFile\n");
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    BlockRegistry::init(nullptr);
    ItemRegistry::init();

    // ChunkSerializer tests
    testEmptyChunkRoundTrip();
    testSingleSubchunkRoundTrip();
    testMultipleSubchunksRoundTrip();
    testNegativeCoordinatesRoundTrip();
    testMixedBlocksInSubchunk();
    testBlockStateRoundTrip();
    testChestBlockStateRoundTrip();
    testBareStateNameLoadsDefaultBlockState();
    testInvalidPackedPaletteIndexRejected();
    testFluidLayerRoundTrip();
    testPayloadSizeReasonable();

    // SavePaths tests
    testSavePathsSanitizeWorldName();
    testSavePathsChunkPath();
    testSavePathsEnsureDirectories();

    // SaveManager tests
    testSaveManagerLevelMeta();
    testSaveManagerChunkRoundTrip();
    testSaveManagerNonexistentChunk();
    testSaveManagerPersistentEntitiesRoundTrip();
    testSaveManagerBlockEntitiesRoundTrip();

    // PlayerSerializer tests
    testPlayerSerializerRoundTrip();
    testPlayerSerializerFileRoundTrip();
    testPlayerSerializerNonexistentFile();

    std::printf("\nAll save system tests passed!\n");
    return 0;
}
