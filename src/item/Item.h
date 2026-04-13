#ifndef MECRAFT_ITEM_H
#define MECRAFT_ITEM_H

#include <array>
#include <cstdint>
#include <string>

#include "../world/Block.h"

using ItemID = uint8_t;

namespace ItemType {
constexpr ItemID AIR = BlockType::AIR;
constexpr ItemID COAL = 200;
constexpr ItemID IRON_PICKAXE = 201;
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
    static std::array<ItemDef, BlockType::COUNT> s_items;
    static std::array<std::string, BlockType::COUNT> s_itemNames;
    static std::array<std::string, BlockType::COUNT> s_itemIconTextureNames;
    static bool s_initializing;
    static bool s_initialized;
};

#endif // MECRAFT_ITEM_H

