// Unit tests for the save system: ChunkSerializer round-trip, SavePaths, SaveManager, PlayerSerializer.

#include "save/ChunkSerializer.h"
#include "save/SavePaths.h"
#include "save/SaveManager.h"
#include "save/SaveFormat.h"
#include "save/PlayerSerializer.h"
#include "world/chunk/Chunk.h"
#include "world/block/Block.h"
#include "item/Item.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>

// ---------------------------------------------------------------------------
// ChunkSerializer round-trip tests
// ---------------------------------------------------------------------------

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
    // Header is 20 bytes, payload should be modest (< 500 bytes for a mostly-empty chunk)
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

    // PlayerSerializer tests
    testPlayerSerializerRoundTrip();
    testPlayerSerializerFileRoundTrip();
    testPlayerSerializerNonexistentFile();

    std::printf("\nAll save system tests passed!\n");
    return 0;
}
