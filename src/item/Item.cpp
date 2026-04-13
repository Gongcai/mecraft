#include "Item.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <nlohmann/json.hpp>

std::array<ItemDef, ItemType::COUNT> ItemRegistry::s_items{};
std::array<std::string, ItemType::COUNT> ItemRegistry::s_itemNames{};
std::array<std::string, ItemType::COUNT> ItemRegistry::s_itemIconTextureNames{};
bool ItemRegistry::s_initializing = false;
bool ItemRegistry::s_initialized = false;

std::array<BlockDropEntry, BlockType::COUNT> BlockDropTable::s_drops{};
bool BlockDropTable::s_initialized = false;

namespace {
constexpr const char* kItemsConfigPath = "../assets/config/items.json";

bool parseNumericString(const std::string& text, int& outValue) {
    if (text.empty()) {
        return false;
    }
    for (const char ch : text) {
        if (ch < '0' || ch > '9') {
            return false;
        }
    }
    outValue = std::stoi(text);
    return true;
}

bool resolveItemToken(const nlohmann::json& value, ItemID& outId) {
    if (value.is_number_integer()) {
        const int id = value.get<int>();
        if (id >= 0 && id < static_cast<int>(ItemType::COUNT)) {
            outId = static_cast<ItemID>(id);
            return true;
        }
        return false;
    }

    if (!value.is_string()) {
        return false;
    }

    const std::string token = value.get<std::string>();
    int id = 0;
    if (parseNumericString(token, id)) {
        if (id >= 0 && id < static_cast<int>(ItemType::COUNT)) {
            outId = static_cast<ItemID>(id);
            return true;
        }
        return false;
    }

    return ItemRegistry::tryGetIdByName(token, outId);
}

bool resolveBlockToken(const nlohmann::json& value, BlockID& outId) {
    if (value.is_number_integer()) {
        const int id = value.get<int>();
        if (id >= 0 && id < static_cast<int>(BlockType::COUNT)) {
            outId = static_cast<BlockID>(id);
            return true;
        }
        return false;
    }

    if (!value.is_string()) {
        return false;
    }

    const std::string token = value.get<std::string>();
    int id = 0;
    if (parseNumericString(token, id)) {
        if (id >= 0 && id < static_cast<int>(BlockType::COUNT)) {
            outId = static_cast<BlockID>(id);
            return true;
        }
        return false;
    }

    return BlockRegistry::tryGetIdByName(token, outId);
}

bool isKnownBlockId(const BlockID id) {
    if (id >= BlockType::COUNT) {
        return false;
    }
    if (id == BlockType::AIR) {
        return true;
    }

    const BlockDef& blockDef = BlockRegistry::get(id);
    return std::strcmp(blockDef.name, "unknown") != 0;
}

ItemDef makeBlockBackedItemDef(const BlockID blockId) {
    const BlockDef& blockDef = BlockRegistry::get(blockId);
    ItemDef def{};
    def.name = blockDef.name;
    def.iconTextureName = "unknown";
    def.maxStack = blockId == BlockType::AIR ? 0 : 64;
    def.iconItemId = static_cast<ItemID>(blockId);
    def.placeBlock = blockId;
    def.renderBlock = blockId;
    return def;
}
}

void ItemRegistry::init() {
    if (s_initialized || s_initializing) {
        return;
    }
    s_initializing = true;

    // Ensure block names/defs are available for block->item fallback.
    BlockRegistry::init(nullptr);

    for (size_t i = 0; i < s_items.size(); ++i) {
        s_items[i] = {};
        s_itemNames[i] = "unknown";
        s_itemIconTextureNames[i] = "unknown";
        s_items[i].name = s_itemNames[i].c_str();
        s_items[i].iconTextureName = s_itemIconTextureNames[i].c_str();
        s_items[i].maxStack = 0;
        s_items[i].iconItemId = ItemType::AIR;
        s_items[i].placeBlock = BlockType::AIR;
        s_items[i].renderBlock = BlockType::AIR;
    }

    s_itemNames[ItemType::AIR] = "air";
    s_itemIconTextureNames[ItemType::AIR] = "air";
    s_items[ItemType::AIR].name = s_itemNames[ItemType::AIR].c_str();
    s_items[ItemType::AIR].iconTextureName = s_itemIconTextureNames[ItemType::AIR].c_str();

    std::ifstream file(kItemsConfigPath);
    if (!file.is_open()) {
        s_initializing = false;
        s_initialized = true;
        return;
    }

    nlohmann::json root;
    try {
        file >> root;
    } catch (...) {
        s_initializing = false;
        s_initialized = true;
        return;
    }

    if (!root.contains("items") || !root["items"].is_array()) {
        s_initializing = false;
        s_initialized = true;
        return;
    }

    for (const auto& itemJson : root["items"]) {
        if (!itemJson.contains("id") || !itemJson["id"].is_number_integer()) {
            continue;
        }

        const int idInt = itemJson["id"].get<int>();
        if (idInt < 0 || idInt >= static_cast<int>(ItemType::COUNT)) {
            continue;
        }

        const ItemID id = static_cast<ItemID>(idInt);
        ItemDef def = s_items[id];

        if (itemJson.contains("name") && itemJson["name"].is_string()) {
            s_itemNames[id] = itemJson["name"].get<std::string>();
            def.name = s_itemNames[id].c_str();
        }

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
    }

    s_initializing = false;
    s_initialized = true;
}

const ItemDef& ItemRegistry::get(const ItemID id) {
    if (!s_initialized) {
        init();
    }
    if (id >= ItemType::COUNT) {
        return s_items[ItemType::AIR];
    }

    if (s_itemNames[id] == "unknown") {
        const BlockID blockId = static_cast<BlockID>(id);
        if (isKnownBlockId(blockId)) {
            ItemDef def = makeBlockBackedItemDef(blockId);
            s_itemNames[id] = def.name;
            s_itemIconTextureNames[id] = def.iconTextureName;
            def.name = s_itemNames[id].c_str();
            def.iconTextureName = s_itemIconTextureNames[id].c_str();
            s_items[id] = def;
        }
    }

    return s_items[id];
}

ItemID ItemRegistry::findByName(const std::string& name) {
    ItemID id = ItemType::AIR;
    if (!tryGetIdByName(name, id)) {
        return ItemType::AIR;
    }
    return id;
}

bool ItemRegistry::tryGetIdByName(const std::string& name, ItemID& outId) {
    if (!s_initialized && !s_initializing) {
        init();
    }

    for (size_t i = 0; i < s_itemNames.size(); ++i) {
        if (s_itemNames[i] == name) {
            outId = static_cast<ItemID>(i);
            return true;
        }
    }

    BlockID blockId = BlockType::AIR;
    if (BlockRegistry::tryGetIdByName(name, blockId)) {
        outId = static_cast<ItemID>(blockId);
        return true;
    }

    return false;
}

ItemID ItemRegistry::fromBlock(const BlockID blockId) {
    return static_cast<ItemID>(blockId);
}

BlockID ItemRegistry::toPlaceBlock(const ItemID itemId) {
    return get(itemId).placeBlock;
}

BlockID ItemRegistry::toRenderBlock(const ItemID itemId) {
    return get(itemId).renderBlock;
}

// ─── BlockDropTable ──────────────────────────────────────────────────────────

void BlockDropTable::init() {
    if (s_initialized) return;

    // Default: each block drops its block-backed item (same ID).
    for (size_t i = 0; i < BlockType::COUNT; ++i) {
        s_drops[i].dropItem = static_cast<ItemID>(i);
        s_drops[i].minCount = 1;
        s_drops[i].maxCount = 1;
    }

    // Override from BlockRegistry drop names.
    for (size_t i = 0; i < BlockType::COUNT; ++i) {
        const std::string& dropName = BlockRegistry::getBlockDropName(static_cast<BlockID>(i));
        if (dropName.empty()) continue;
        ItemID resolved = ItemType::AIR;
        if (ItemRegistry::tryGetIdByName(dropName, resolved)) {
            s_drops[i].dropItem = resolved;
        }
    }

    s_initialized = true;
}

ItemID BlockDropTable::getDropItem(const BlockID blockId) {
    if (!s_initialized) init();
    if (blockId >= BlockType::COUNT) return ItemType::AIR;
    return s_drops[blockId].dropItem;
}

const BlockDropEntry& BlockDropTable::get(const BlockID blockId) {
    if (!s_initialized) init();
    if (blockId >= BlockType::COUNT) return s_drops[0];
    return s_drops[blockId];
}

