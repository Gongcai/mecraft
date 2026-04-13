#ifndef MECRAFT_ITEM_H
#define MECRAFT_ITEM_H

#include <array>
#include <cstdint>
#include <string>

#include "../world/Block.h"

using ItemID = uint16_t;

namespace ItemType {
constexpr ItemID AIR = 0;
constexpr ItemID COAL = 256;
constexpr ItemID IRON_PICKAXE = 257;
constexpr uint16_t COUNT = 4096;
}

struct ItemStack {
    ItemID itemId = ItemType::AIR;
    uint16_t count = 0;
    uint16_t durability = 0;

    [[nodiscard]] bool isEmpty() const {
        return itemId == ItemType::AIR || count == 0;
    }
};

struct ItemDef {
    const char* name = "unknown";
    const char* iconTextureName = "unknown";
    uint16_t maxStack = 64;
    ItemID iconItemId = ItemType::AIR;
    BlockID placeBlock = BlockType::AIR;
    BlockID renderBlock = BlockType::AIR;
    bool isTool = false;
    uint16_t maxDurability = 0;
};

// Block drop table entry: describes what item a block drops when broken.
struct BlockDropEntry {
    ItemID dropItem = 0;   // defaults to ItemType::AIR (0)
    uint8_t minCount = 1;
    uint8_t maxCount = 1;
};

// Maps BlockID → drop item info. Initialized after BlockRegistry and ItemRegistry.
class BlockDropTable {
public:
    static void init();
    [[nodiscard]] static ItemID getDropItem(BlockID blockId);
    [[nodiscard]] static const BlockDropEntry& get(BlockID blockId);
private:
    static std::array<BlockDropEntry, BlockType::COUNT> s_drops;
    static bool s_initialized;
};

class ItemRegistry {
public:
    // Loads item definitions from assets/config/items.json.
    static void init();
    [[nodiscard]] static const ItemDef& get(ItemID id);
    [[nodiscard]] static ItemID findByName(const std::string& name);
    [[nodiscard]] static bool tryGetIdByName(const std::string& name, ItemID& outId);
    [[nodiscard]] static ItemID fromBlock(BlockID blockId);
    [[nodiscard]] static BlockID toPlaceBlock(ItemID itemId);
    [[nodiscard]] static BlockID toRenderBlock(ItemID itemId);

private:
    static std::array<ItemDef, ItemType::COUNT> s_items;
    static std::array<std::string, ItemType::COUNT> s_itemNames;
    static std::array<std::string, ItemType::COUNT> s_itemIconTextureNames;
    static bool s_initializing;
    static bool s_initialized;
};

#endif // MECRAFT_ITEM_H

