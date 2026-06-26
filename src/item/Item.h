#ifndef MECRAFT_ITEM_H
#define MECRAFT_ITEM_H

#include <vector>
#include <cstdint>
#include <string>
#include <unordered_map>

#include "../world/block/Block.h"
#include "../engine/registry/NamespacedId.h"
#include "../game/content/BuiltinIds.h"
#include "../engine/registry/IdRegistry.h"

// ItemID is now a separate RuntimeId from BlockID
using ItemID = RuntimeId;

// Item ID constants
namespace ItemIds {
    extern ItemID AIR;
#define MECRAFT_DECLARE_PURE_ITEM_ID(symbol, path) extern ItemID symbol;
    MECRAFT_FOR_EACH_BUILTIN_PURE_ITEM(MECRAFT_DECLARE_PURE_ITEM_ID)
#undef MECRAFT_DECLARE_PURE_ITEM_ID

    void init();  // Called after ItemRegistry::init()
}

struct ItemStack {
    ItemID itemId = 0;  // Will be set to ItemIds::AIR after init
    uint16_t count = 0;
    uint16_t durability = 0;

    [[nodiscard]] bool isEmpty() const {
        return itemId == 0 || count == 0;
    }
};

struct ItemDef {
    NamespacedId namespacedId = NamespacedId("minecraft", "unknown");
    const char* iconTextureName = "unknown";
    uint16_t maxStack = 64;
    ItemID iconItemId = 0;  // AIR
    BlockID placeBlock = 0; // AIR
    BlockID renderBlock = 0; // AIR
    bool isTool = false;
    std::string toolKind;
    uint8_t toolTier = 0;
    float toolEfficiency = 1.0f;
    uint16_t maxDurability = 0;
    std::vector<NamespacedId> tags;
};

// Block drop table entry
struct BlockDropEntry {
    ItemID dropItem = 0;   // defaults to AIR (0)
    uint8_t minCount = 1;
    uint8_t maxCount = 1;
};

// Maps BlockID → drop item info
class BlockDropTable {
public:
    static void init();
    [[nodiscard]] static ItemID getDropItem(BlockID blockId);
    [[nodiscard]] static const BlockDropEntry& get(BlockID blockId);
private:
    static std::unordered_map<BlockID, BlockDropEntry> s_drops;
    static bool s_initialized;
};

class ItemRegistry {
public:
    static void init();
    [[nodiscard]] static const ItemDef& get(ItemID id);
    [[nodiscard]] static ItemID findByName(const std::string& name);
    [[nodiscard]] static ItemID requireIdByName(const std::string& name);
    [[nodiscard]] static bool tryGetIdByName(const std::string& name, ItemID& outId);
    [[nodiscard]] static ItemID getId(const NamespacedId& namespacedId);
    [[nodiscard]] static bool tryGetId(const NamespacedId& namespacedId, ItemID& outId);
    [[nodiscard]] static const NamespacedId& getNamespacedId(ItemID runtimeId);

    // Explicit Block↔Item mapping
    static void registerBlockItem(BlockID blockId, ItemID itemId);
    [[nodiscard]] static ItemID fromBlock(BlockID blockId);
    [[nodiscard]] static BlockID toPlaceBlock(ItemID itemId);
    [[nodiscard]] static BlockID toRenderBlock(ItemID itemId);
    [[nodiscard]] static bool hasTag(ItemID itemId, const NamespacedId& tag);

    // Register a new item (Mod API)
    static ItemID registerItem(const NamespacedId& id, ItemDef def);

    // Get the number of registered items
    [[nodiscard]] static size_t getItemCount();

private:
    static IdRegistry s_idRegistry;
    static std::vector<ItemDef> s_items;                        // index = ItemID
    static std::vector<std::string> s_itemIconTextureNames;     // for stable c_str storage
    static std::unordered_map<NamespacedId, ItemID> s_idLookup;
    static bool s_initializing;
    static bool s_initialized;

    // Explicit Block↔Item mapping tables
    static std::unordered_map<BlockID, ItemID> s_blockToItem;
    static std::unordered_map<ItemID, BlockID> s_itemToPlaceBlock;
    static std::unordered_map<ItemID, BlockID> s_itemToRenderBlock;
};

#endif // MECRAFT_ITEM_H
