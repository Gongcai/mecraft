#include "Item.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <nlohmann/json.hpp>

std::array<ItemDef, BlockType::COUNT> ItemRegistry::s_items{};
std::array<std::string, BlockType::COUNT> ItemRegistry::s_itemNames{};
std::array<std::string, BlockType::COUNT> ItemRegistry::s_itemIconTextureNames{};
bool ItemRegistry::s_initializing = false;
bool ItemRegistry::s_initialized = false;

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
        if (id >= 0 && id < static_cast<int>(BlockType::COUNT)) {
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
        if (id >= 0 && id < static_cast<int>(BlockType::COUNT)) {
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
}

void ItemRegistry::init() {
    if (s_initialized || s_initializing) {
        return;
    }
    s_initializing = true;

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
        if (idInt < 0 || idInt >= static_cast<int>(BlockType::COUNT)) {
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
    if (id >= BlockType::COUNT) {
        return s_items[ItemType::AIR];
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

