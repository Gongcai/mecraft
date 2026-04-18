//
// Created by Caiwe on 2026/3/24.
//

#ifndef MECRAFT_BLOCK_H
#define MECRAFT_BLOCK_H
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>

#include "../core/NamespacedId.h"
#include "../core/IdRegistry.h"

class ResourceMgr;

// BlockID is now RuntimeId (uint16_t), allowing up to 65535 block types
using BlockID = RuntimeId;

// Block ID constants — initialized after BlockRegistry::init()
namespace BlockIds {
    extern BlockID AIR;
    extern BlockID DIRT;
    extern BlockID GRASS;
    extern BlockID STONE;
    extern BlockID SAND;
    extern BlockID WOOD;
    extern BlockID GLASS;
    extern BlockID COAL_ORE;
    extern BlockID DIAMOND_ORE;
    extern BlockID GOLD_ORE;
    extern BlockID IRON_ORE;
    extern BlockID WATER;
    extern BlockID BEDROCK;
    extern BlockID TALL_GRASS;
    extern BlockID ROSE;
    extern BlockID OAK_PLANKS;
    extern BlockID SPRUCE_PLANKS;
    extern BlockID BIRCH_PLANKS;
    extern BlockID JUNGLE_PLANKS;
    extern BlockID ACACIA_PLANKS;
    extern BlockID DARK_OAK_PLANKS;
    extern BlockID MANGROVE_PLANKS;
    extern BlockID CHERRY_PLANKS;
    extern BlockID PALE_OAK_PLANKS;
    extern BlockID BAMBOO_PLANKS;
    extern BlockID CRIMSON_PLANKS;
    extern BlockID WARPED_PLANKS;
    extern BlockID BIRCH_LOG;
    extern BlockID TORCH;
    extern BlockID BROWN_MUSHROOM;

    void init();  // Called after BlockRegistry::init()
}

enum class BlockRenderShape : uint8_t {
    Cube = 0,
    Cross = 1
};

struct BlockDef {
    NamespacedId namespacedId = NamespacedId("minecraft", "unknown");
    bool isSolid        = true;
    bool isTransparent  = false;
    bool isLightSource  = false;
    bool isSelectable   = true;
    BlockRenderShape renderShape = BlockRenderShape::Cube;
    bool useGrassTint = false;
    uint8_t lightLevel  = 0;
    uint8_t opacity     = 0;
    uint16_t timeToBreak = 1000;
    // Six face texture atlas tile indices
    int texTop = 0;
    int texBottom = 0;
    int texLeft = 0;
    int texRight = 0;
    int texFront = 0;
    int texBack = 0;
};

class BlockRegistry {
public:
    static void init(ResourceMgr* resourceMgr = nullptr);
    static void ensureInitialized();
    static const BlockDef& get(BlockID id);
    static const BlockDef& getFast(BlockID id) {
        return id < s_blocks.size() ? s_blocks[id] : s_blocks[0];
    }
    [[nodiscard]] static uint8_t getOpacityFast(BlockID id) {
        return id < s_blocks.size() ? s_blocks[id].opacity : 0;
    }
    [[nodiscard]] static uint8_t getLightLevelFast(BlockID id) {
        return id < s_blocks.size() ? s_blocks[id].lightLevel : 0;
    }
    [[nodiscard]] static bool isLightSourceFast(BlockID id) {
        return id < s_blocks.size() && s_blocks[id].isLightSource;
    }
    [[nodiscard]] static BlockID findByName(const std::string& name);
    [[nodiscard]] static bool tryGetIdByName(const std::string& name, BlockID& outId);
    [[nodiscard]] static BlockID getId(const NamespacedId& namespacedId);
    [[nodiscard]] static bool tryGetId(const NamespacedId& namespacedId, BlockID& outId);
    [[nodiscard]] static const NamespacedId& getNamespacedId(BlockID runtimeId);
    [[nodiscard]] static const NamespacedId& getBlockDropId(BlockID id);
    static void printAllBlocks();

    // Register a new block (Mod API)
    static BlockID registerBlock(const NamespacedId& id, BlockDef def);

    // Get the number of registered blocks
    [[nodiscard]] static size_t getBlockCount();

private:
    static IdRegistry s_idRegistry;
    static std::vector<BlockDef> s_blocks;                        // index = BlockID (RuntimeId)
    static std::vector<NamespacedId> s_blockDropIds;               // index = BlockID
    static std::unordered_map<NamespacedId, BlockID> s_idLookup;  // fast reverse lookup
    static bool s_initialized;
};


#endif //MECRAFT_BLOCK_H
