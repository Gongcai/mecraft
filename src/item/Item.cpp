#include "Item.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <nlohmann/json.hpp>

IdRegistry ItemRegistry::s_idRegistry{};
std::vector<ItemDef> ItemRegistry::s_items{};
std::vector<std::string> ItemRegistry::s_itemIconTextureNames{};
std::unordered_map<NamespacedId, ItemID> ItemRegistry::s_idLookup{};
bool ItemRegistry::s_initializing = false;
bool ItemRegistry::s_initialized = false;

std::unordered_map<BlockID, ItemID> ItemRegistry::s_blockToItem{};
std::unordered_map<ItemID, BlockID> ItemRegistry::s_itemToPlaceBlock{};
std::unordered_map<ItemID, BlockID> ItemRegistry::s_itemToRenderBlock{};

std::unordered_map<BlockID, BlockDropEntry> BlockDropTable::s_drops{};
bool BlockDropTable::s_initialized = false;

namespace ItemIds {
ItemID AIR = 0;
ItemID COAL = 30;
ItemID IRON_PICKAXE = 31;

void init() {
    AIR = ItemRegistry::getId(NamespacedId("minecraft", "air"));
    COAL = ItemRegistry::getId(NamespacedId("minecraft", "coal"));
    IRON_PICKAXE = ItemRegistry::getId(NamespacedId("minecraft", "iron_pickaxe"));
}
}

namespace {
constexpr const char* kItemsConfigPath = "../assets/config/items.json";

bool resolveItemToken(const nlohmann::json& value, ItemID& outId) {
    if (!value.is_string()) {
        return false;
    }

    const std::string token = value.get<std::string>();

    // Try as NamespacedId (contains ':')
    if (token.find(':') != std::string::npos) {
        NamespacedId nsId(token);
        if (ItemRegistry::tryGetId(nsId, outId)) {
            return true;
        }
    }

    return ItemRegistry::tryGetIdByName(token, outId);
}

bool resolveBlockToken(const nlohmann::json& value, BlockID& outId) {
    if (!value.is_string()) {
        return false;
    }

    const std::string token = value.get<std::string>();

    // Try as NamespacedId
    if (token.find(':') != std::string::npos) {
        NamespacedId nsId(token);
        if (BlockRegistry::tryGetId(nsId, outId)) {
            return true;
        }
    }

    return BlockRegistry::tryGetIdByName(token, outId);
}
}

void ItemRegistry::init() {
    if (s_initialized || s_initializing) {
        return;
    }
    s_initializing = true;

    // Ensure block registry is initialized first
    BlockRegistry::init(nullptr);

    // Step 1: Register all built-in item IDs
    s_idRegistry.initBuiltinItemIds();

    // Step 2: Create default ItemDef entries
    s_items.resize(s_idRegistry.size());
    s_itemIconTextureNames.resize(s_idRegistry.size());

    for (size_t i = 0; i < s_items.size(); ++i) {
        s_items[i] = ItemDef{};
        s_items[i].namespacedId = s_idRegistry.getNamespacedId(static_cast<ItemID>(i));
        s_itemIconTextureNames[i] = "unknown";
        s_items[i].iconTextureName = s_itemIconTextureNames[i].c_str();
        s_items[i].maxStack = 0;
    }

    // Step 3: Set up block-backed items (IDs 1-29 correspond to blocks 1-29)
    for (size_t i = 1; i < 30 && i < s_items.size(); ++i) {
        const BlockID blockId = static_cast<BlockID>(i);
        const BlockDef& blockDef = BlockRegistry::get(blockId);
        ItemDef& itemDef = s_items[i];
        itemDef.namespacedId = s_idRegistry.getNamespacedId(static_cast<ItemID>(i));
        itemDef.maxStack = 64;
        itemDef.placeBlock = blockId;
        itemDef.renderBlock = blockId;
        itemDef.iconItemId = static_cast<ItemID>(i);

        // Register explicit mapping
        registerBlockItem(blockId, static_cast<ItemID>(i));
    }

    // AIR item
    s_items[0].maxStack = 0;

    // Build idLookup
    s_idLookup.clear();
    for (size_t i = 0; i < s_idRegistry.size(); ++i) {
        s_idLookup[s_idRegistry.getNamespacedId(static_cast<ItemID>(i))] = static_cast<ItemID>(i);
    }

    // Step 4: Load items.json to override defaults
    std::ifstream file(kItemsConfigPath);
    if (!file.is_open()) {
        s_initializing = false;
        s_initialized = true;
        ItemIds::init();
        return;
    }

    nlohmann::json root;
    try {
        file >> root;
    } catch (...) {
        s_initializing = false;
        s_initialized = true;
        ItemIds::init();
        return;
    }

    if (!root.contains("items") || !root["items"].is_array()) {
        s_initializing = false;
        s_initialized = true;
        ItemIds::init();
        return;
    }

    for (const auto& itemJson : root["items"]) {
        ItemID id = 0;
        bool found = false;

        if (itemJson.contains("id")) {
            if (itemJson["id"].is_string()) {
                const std::string idStr = itemJson["id"].get<std::string>();
                NamespacedId nsId(idStr);
                auto it = s_idLookup.find(nsId);
                if (it != s_idLookup.end()) {
                    id = it->second;
                    found = true;
                } else {
                    id = registerItem(nsId, ItemDef{});
                    found = true;
                }
            }
        }

        if (!found) continue;

        if (id >= s_items.size()) {
            s_items.resize(id + 1);
            s_itemIconTextureNames.resize(id + 1);
        }

        ItemDef def = s_items[id];
        def.namespacedId = s_idRegistry.getNamespacedId(id);

        if (itemJson.contains("iconTexture") && itemJson["iconTexture"].is_string()) {
            s_itemIconTextureNames[id] = itemJson["iconTexture"].get<std::string>();
            def.iconTextureName = s_itemIconTextureNames[id].c_str();
        }

        if (itemJson.contains("maxStack") && itemJson["maxStack"].is_number_integer()) {
            const int maxStack = itemJson["maxStack"].get<int>();
            def.maxStack = static_cast<uint16_t>(std::max(0, std::min(maxStack, 65535)));
        }

        if (itemJson.contains("isTool") && itemJson["isTool"].is_boolean()) {
            def.isTool = itemJson["isTool"].get<bool>();
        }

        if (itemJson.contains("maxDurability") && itemJson["maxDurability"].is_number_integer()) {
            const int durability = itemJson["maxDurability"].get<int>();
            def.maxDurability = static_cast<uint16_t>(std::max(0, std::min(durability, 65535)));
        }

        if (itemJson.contains("icon")) {
            ItemID iconId = def.iconItemId;
            if (resolveItemToken(itemJson["icon"], iconId)) {
                def.iconItemId = iconId;
            }
        }

        if (itemJson.contains("placeBlock")) {
            BlockID placeBlock = def.placeBlock;
            if (resolveBlockToken(itemJson["placeBlock"], placeBlock)) {
                def.placeBlock = placeBlock;
            }
        }

        if (itemJson.contains("renderBlock")) {
            BlockID renderBlock = def.renderBlock;
            if (resolveBlockToken(itemJson["renderBlock"], renderBlock)) {
                def.renderBlock = renderBlock;
            }
        }

        s_items[id] = def;

        // Update mappings
        s_itemToPlaceBlock[id] = def.placeBlock;
        s_itemToRenderBlock[id] = def.renderBlock;
    }

    s_initializing = false;
    s_initialized = true;
    ItemIds::init();
}

const ItemDef& ItemRegistry::get(const ItemID id) {
    if (!s_initialized) {
        init();
    }
    if (id >= s_items.size()) {
        return s_items[0];  // AIR
    }
    return s_items[id];
}

ItemID ItemRegistry::findByName(const std::string& name) {
    ItemID id = 0;
    if (!tryGetIdByName(name, id)) {
        return 0;
    }
    return id;
}

bool ItemRegistry::tryGetIdByName(const std::string& name, ItemID& outId) {
    if (!s_initialized && !s_initializing) {
        init();
    }

    // Try as NamespacedId
    if (name.find(':') != std::string::npos) {
        NamespacedId nsId(name);
        return tryGetId(nsId, outId);
    }

    // Try as path with default namespace
    NamespacedId nsId("minecraft", name);
    return tryGetId(nsId, outId);
}

ItemID ItemRegistry::getId(const NamespacedId& namespacedId) {
    if (!s_initialized) init();
    auto it = s_idLookup.find(namespacedId);
    if (it != s_idLookup.end()) {
        return it->second;
    }
    return 0;
}

bool ItemRegistry::tryGetId(const NamespacedId& namespacedId, ItemID& outId) {
    if (!s_initialized) init();
    auto it = s_idLookup.find(namespacedId);
    if (it != s_idLookup.end()) {
        outId = it->second;
        return true;
    }
    return false;
}

const NamespacedId& ItemRegistry::getNamespacedId(ItemID runtimeId) {
    return s_idRegistry.getNamespacedId(runtimeId);
}

void ItemRegistry::registerBlockItem(BlockID blockId, ItemID itemId) {
    s_blockToItem[blockId] = itemId;
    s_itemToPlaceBlock[itemId] = blockId;
    s_itemToRenderBlock[itemId] = blockId;
}

ItemID ItemRegistry::fromBlock(const BlockID blockId) {
    auto it = s_blockToItem.find(blockId);
    if (it != s_blockToItem.end()) {
        return it->second;
    }
    return 0;  // AIR
}

BlockID ItemRegistry::toPlaceBlock(const ItemID itemId) {
    auto it = s_itemToPlaceBlock.find(itemId);
    if (it != s_itemToPlaceBlock.end()) {
        return it->second;
    }
    return 0;  // AIR
}

BlockID ItemRegistry::toRenderBlock(const ItemID itemId) {
    auto it = s_itemToRenderBlock.find(itemId);
    if (it != s_itemToRenderBlock.end()) {
        return it->second;
    }
    return 0;  // AIR
}

ItemID ItemRegistry::registerItem(const NamespacedId& id, ItemDef def) {
    auto it = s_idLookup.find(id);
    if (it != s_idLookup.end()) {
        return it->second;
    }

    ItemID runtimeId = s_idRegistry.registerId(id);
    def.namespacedId = id;

    if (runtimeId >= s_items.size()) {
        s_items.resize(runtimeId + 1);
        s_itemIconTextureNames.resize(runtimeId + 1);
    }
    s_items[runtimeId] = def;
    s_idLookup[id] = runtimeId;
    return runtimeId;
}

size_t ItemRegistry::getItemCount() {
    return s_items.size();
}

// ─── BlockDropTable ──────────────────────────────────────────────────────────

void BlockDropTable::init() {
    if (s_initialized) return;

    // Default: each block drops its block-backed item
    for (size_t i = 0; i < BlockRegistry::getBlockCount(); ++i) {
        BlockID blockId = static_cast<BlockID>(i);
        ItemID itemId = ItemRegistry::fromBlock(blockId);
        s_drops[blockId] = {itemId, 1, 1};
    }

    // Override from BlockRegistry drop IDs
    for (size_t i = 0; i < BlockRegistry::getBlockCount(); ++i) {
        BlockID blockId = static_cast<BlockID>(i);
        const NamespacedId& dropId = BlockRegistry::getBlockDropId(blockId);
        ItemID resolved = 0;
        if (ItemRegistry::tryGetId(dropId, resolved)) {
            s_drops[blockId].dropItem = resolved;
        }
    }

    s_initialized = true;
}

ItemID BlockDropTable::getDropItem(const BlockID blockId) {
    if (!s_initialized) init();
    auto it = s_drops.find(blockId);
    if (it == s_drops.end()) return 0;
    return it->second.dropItem;
}

const BlockDropEntry& BlockDropTable::get(const BlockID blockId) {
    if (!s_initialized) init();
    auto it = s_drops.find(blockId);
    if (it == s_drops.end()) {
        static BlockDropEntry empty{0, 1, 1};
        return empty;
    }
    return it->second;
}
