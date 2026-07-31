#include "Item.h"
#include "Paths.h"
#include "../world/fluid/FluidRegistry.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <utility>
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

namespace {
constexpr const char* kItemsConfigPath = ITEMS_CONFIG_PATH;

bool fail(std::string& error, std::string message) {
    error = std::move(message);
    return false;
}

BlockID resolveDropBlockId(const BlockID blockId) {
    if (blockId < BlockRegistry::getBlockCount()) {
        return blockId;
    }
    return 0;
}

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

bool readItemToken(const nlohmann::json& value, const std::string& context, ItemID& outId, std::string& error) {
    if (!resolveItemToken(value, outId)) {
        return fail(error, "Unknown item id in " + context);
    }
    return true;
}

bool readBlockToken(const nlohmann::json& value, const std::string& context, BlockID& outId, std::string& error) {
    if (!resolveBlockToken(value, outId)) {
        return fail(error, "Unknown block id in " + context);
    }
    return true;
}

const nlohmann::json* findRequiredField(const nlohmann::json& owner, const std::string& context, const char* fieldName,
                                        std::string& error) {
    const auto it = owner.find(fieldName);
    if (it == owner.end()) {
        fail(error, context + " is missing required field: " + fieldName);
        return nullptr;
    }
    return &(*it);
}

bool parseItemUseBehavior(const nlohmann::json& value, const std::string& context, ItemUseBehavior& out,
                          std::string& error) {
    if (!value.is_string()) {
        return fail(error, context + ".behavior must be a string");
    }

    const std::string behavior = value.get<std::string>();
    if (behavior == "till_soil") {
        out = ItemUseBehavior::TillSoil;
        return true;
    }
    if (behavior == "bucket_pickup_fluid") {
        out = ItemUseBehavior::BucketPickupFluid;
        return true;
    }
    if (behavior == "bucket_place_fluid") {
        out = ItemUseBehavior::BucketPlaceFluid;
        return true;
    }
    return fail(error, "Unknown item use behavior in " + context + ": " + behavior);
}

bool parseMatchBlocks(const nlohmann::json& ruleJson, const std::string& context, std::vector<BlockID>& outBlocks,
                      std::string& error) {
    const nlohmann::json* matchJson = findRequiredField(ruleJson, context, "match", error);
    if (matchJson == nullptr) {
        return false;
    }
    if (!matchJson->is_object()) {
        return fail(error, context + ".match must be an object");
    }

    const nlohmann::json* blockJson = findRequiredField(*matchJson, context + ".match", "block", error);
    if (blockJson == nullptr) {
        return false;
    }
    std::vector<BlockID> blocks;
    if (blockJson->is_string()) {
        BlockID blockId = RUNTIME_ID_NULL;
        if (!readBlockToken(*blockJson, context + ".match.block", blockId, error)) {
            return false;
        }
        blocks.push_back(blockId);
        outBlocks = std::move(blocks);
        return true;
    }
    if (!blockJson->is_array()) {
        return fail(error, context + ".match.block must be a string or array");
    }
    if (blockJson->empty()) {
        return fail(error, context + ".match.block must not be empty");
    }
    blocks.reserve(blockJson->size());
    for (size_t i = 0; i < blockJson->size(); ++i) {
        BlockID blockId = RUNTIME_ID_NULL;
        if (!readBlockToken((*blockJson)[i], context + ".match.block", blockId, error)) {
            return false;
        }
        blocks.push_back(blockId);
    }
    outBlocks = std::move(blocks);
    return true;
}

bool readDurabilityCost(const nlohmann::json& ruleJson, const std::string& context, uint16_t& out, std::string& error) {
    const nlohmann::json* value = findRequiredField(ruleJson, context, "consume_durability", error);
    if (value == nullptr) {
        return false;
    }
    if (!value->is_number_integer()) {
        return fail(error, context + ".consume_durability must be an integer");
    }
    const int64_t parsed = value->get<int64_t>();
    if (parsed < 0 || parsed > std::numeric_limits<uint16_t>::max()) {
        return fail(error, context + ".consume_durability is out of range");
    }
    out = static_cast<uint16_t>(parsed);
    return true;
}

bool validateFluidResultBlock(const ItemUseRule& rule, const std::string& context, std::string& error) {
    if (FluidRegistry::tryGetByBlock(rule.resultBlock) == nullptr) {
        return fail(error, context + ".result_block must reference a registered fluid block");
    }
    return true;
}

bool readConditionFlag(const nlohmann::json& ruleJson, const std::string& context, const char* conditionName, bool& out,
                       std::string& error) {
    const nlohmann::json* conditionsJson = findRequiredField(ruleJson, context, "conditions", error);
    if (conditionsJson == nullptr) {
        return false;
    }
    if (!conditionsJson->is_object()) {
        return fail(error, context + ".conditions must be an object");
    }
    const nlohmann::json* conditionJson =
        findRequiredField(*conditionsJson, context + ".conditions", conditionName, error);
    if (conditionJson == nullptr) {
        return false;
    }
    if (!conditionJson->is_boolean()) {
        return fail(error, context + ".conditions." + conditionName + " must be a boolean");
    }
    out = conditionJson->get<bool>();
    return true;
}

bool parseUseOnBlockRules(const nlohmann::json& itemJson, const std::string& itemName,
                          std::vector<ItemUseRule>& outRules, std::string& error) {
    const auto rulesIt = itemJson.find("use_on_block");
    if (rulesIt == itemJson.end()) {
        outRules.clear();
        return true;
    }
    if (!rulesIt->is_array()) {
        return fail(error, "use_on_block must be an array for item: " + itemName);
    }

    std::vector<ItemUseRule> rules;
    rules.reserve(rulesIt->size());
    for (size_t i = 0; i < rulesIt->size(); ++i) {
        const nlohmann::json& ruleJson = (*rulesIt)[i];
        const std::string context = itemName + ".use_on_block[" + std::to_string(i) + "]";
        if (!ruleJson.is_object()) {
            return fail(error, context + " must be an object");
        }

        ItemUseRule rule;
        const nlohmann::json* behavior = findRequiredField(ruleJson, context, "behavior", error);
        if (behavior == nullptr || !parseItemUseBehavior(*behavior, context, rule.behavior, error)) {
            return false;
        }

        switch (rule.behavior) {
        case ItemUseBehavior::TillSoil: {
            const nlohmann::json* resultBlock = findRequiredField(ruleJson, context, "result_block", error);
            if (!parseMatchBlocks(ruleJson, context, rule.matchBlocks, error) || resultBlock == nullptr ||
                !readBlockToken(*resultBlock, context + ".result_block", rule.resultBlock, error) ||
                !readDurabilityCost(ruleJson, context, rule.consumeDurability, error) ||
                !readConditionFlag(ruleJson, context, "empty_above", rule.requiresEmptyAbove, error)) {
                return false;
            }
            break;
        }
        case ItemUseBehavior::BucketPickupFluid: {
            const nlohmann::json* resultBlock = findRequiredField(ruleJson, context, "result_block", error);
            const nlohmann::json* resultItem = findRequiredField(ruleJson, context, "result_item", error);
            if (!parseMatchBlocks(ruleJson, context, rule.matchBlocks, error) || resultBlock == nullptr ||
                resultItem == nullptr ||
                !readBlockToken(*resultBlock, context + ".result_block", rule.resultBlock, error) ||
                !readItemToken(*resultItem, context + ".result_item", rule.resultItem, error) ||
                !readConditionFlag(ruleJson, context, "source_fluid", rule.requiresSourceFluid, error)) {
                return false;
            }
            break;
        }
        case ItemUseBehavior::BucketPlaceFluid: {
            const nlohmann::json* resultBlock = findRequiredField(ruleJson, context, "result_block", error);
            const nlohmann::json* resultItem = findRequiredField(ruleJson, context, "result_item", error);
            if (resultBlock == nullptr || resultItem == nullptr ||
                !readBlockToken(*resultBlock, context + ".result_block", rule.resultBlock, error) ||
                !validateFluidResultBlock(rule, context, error) ||
                !readItemToken(*resultItem, context + ".result_item", rule.resultItem, error) ||
                !readConditionFlag(ruleJson, context, "can_place_fluid", rule.requiresFluidPlacement, error)) {
                return false;
            }
            break;
        }
        }

        rules.push_back(std::move(rule));
    }
    outRules = std::move(rules);
    return true;
}

bool parseItemTagList(const nlohmann::json& ownerJson, const std::string& ownerName, std::vector<NamespacedId>& outTags,
                      std::string& error) {
    std::vector<NamespacedId> tags;
    const auto tagsIt = ownerJson.find("tags");
    if (tagsIt == ownerJson.end()) {
        outTags.clear();
        return true;
    }
    if (!tagsIt->is_array()) {
        return fail(error, "Item tags must be an array: " + ownerName);
    }

    for (const nlohmann::json& tagJson : *tagsIt) {
        if (!tagJson.is_string()) {
            return fail(error, "Item tag entries must be strings: " + ownerName);
        }
        tags.emplace_back(tagJson.get<std::string>());
    }
    outTags = std::move(tags);
    return true;
}

void appendUniqueTags(std::vector<NamespacedId>& target, const std::vector<NamespacedId>& tags) {
    for (const NamespacedId& tag : tags) {
        if (std::find(target.begin(), target.end(), tag) == target.end()) {
            target.push_back(tag);
        }
    }
}
} // namespace

bool ItemRegistry::init() {
    if (s_initialized || s_initializing) {
        return true;
    }
    s_initializing = true;

    // Ensure block registry is initialized first
    BlockRegistry::init(nullptr);

    nlohmann::json root;
    bool hasItemConfig = false;
    std::ifstream file(kItemsConfigPath);
    if (file.is_open()) {
        root = nlohmann::json::parse(file, nullptr, false);
        if (!root.is_discarded()) {
            hasItemConfig = root.contains("items") && root["items"].is_array();
        } else {
            std::cerr << "Failed to parse items.json: invalid JSON\n";
            s_initializing = false;
            return false;
        }
    }

    if (!hasItemConfig) {
        std::cerr << "items.json must contain an items array.\n";
        s_initializing = false;
        return false;
    }

    if (hasItemConfig) {
        for (const auto& itemJson : root["items"]) {
            if (!itemJson.contains("id") || !itemJson["id"].is_string()) {
                continue;
            }
            const NamespacedId nsId(itemJson["id"].get<std::string>());
            s_idRegistry.registerId(nsId);
        }
    }

    if (s_idRegistry.size() == 0 || s_idRegistry.getNamespacedId(RUNTIME_ID_NULL) != NamespacedId("minecraft", "air")) {
        std::cerr << "items.json must register minecraft:air as RuntimeId 0.\n";
        s_initializing = false;
        return false;
    }

    // Step 1: Create default ItemDef entries for JSON-declared items.
    s_items.resize(s_idRegistry.size());
    s_itemIconTextureNames.resize(s_idRegistry.size());

    for (size_t i = 0; i < s_items.size(); ++i) {
        s_items[i] = ItemDef{};
        s_items[i].namespacedId = s_idRegistry.getNamespacedId(static_cast<ItemID>(i));
        s_itemIconTextureNames[i] = "unknown";
        s_items[i].iconTextureName = s_itemIconTextureNames[i].c_str();
        s_items[i].maxStack = 0;
    }

    // AIR item
    s_items[0].maxStack = 0;

    // Build idLookup
    s_idLookup.clear();
    for (size_t i = 0; i < s_idRegistry.size(); ++i) {
        s_idLookup[s_idRegistry.getNamespacedId(static_cast<ItemID>(i))] = static_cast<ItemID>(i);
    }

    auto ensureUnknownIconStorage = [&](const ItemID itemId) {
        if (itemId >= s_itemIconTextureNames.size()) {
            s_itemIconTextureNames.resize(itemId + 1);
        }
        if (s_itemIconTextureNames[itemId].empty()) {
            s_itemIconTextureNames[itemId] = "unknown";
        }
    };

    auto configureBlockBackedItem = [&](const BlockID blockId, const ItemID itemId) {
        ensureUnknownIconStorage(itemId);

        if (itemId >= s_items.size()) {
            s_items.resize(itemId + 1);
        }

        ItemDef& itemDef = s_items[itemId];
        itemDef.namespacedId = s_idRegistry.getNamespacedId(itemId);
        itemDef.iconTextureName = s_itemIconTextureNames[itemId].c_str();
        itemDef.maxStack = 64;
        itemDef.iconItemId = itemId;
        itemDef.placeBlock = blockId;
        itemDef.renderBlock = blockId;
        itemDef.tags = BlockRegistry::getFast(blockId).tags;

        registerBlockItem(blockId, itemId);
    };

    // Step 2: Every registered block gets a default block-backed item.
    // JSON-declared item IDs keep their data-file order; remaining block items are synthesized after them.
    for (size_t i = 1; i < BlockRegistry::getBlockCount(); ++i) {
        const BlockID blockId = static_cast<BlockID>(i);
        const NamespacedId& blockNsId = BlockRegistry::getNamespacedId(blockId);

        ItemID itemId = 0;
        auto itemIt = s_idLookup.find(blockNsId);
        if (itemIt != s_idLookup.end()) {
            itemId = itemIt->second;
        } else {
            itemId = registerItem(blockNsId, ItemDef{});
            ensureUnknownIconStorage(itemId);
        }

        configureBlockBackedItem(blockId, itemId);
    }

    // Step 3: Load items.json to override defaults.

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

        if (!found)
            continue;

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

        if (itemJson.contains("toolKind") && itemJson["toolKind"].is_string()) {
            def.toolKind = itemJson["toolKind"].get<std::string>();
            def.isTool = true;
        }

        if (itemJson.contains("toolTier") && itemJson["toolTier"].is_number_integer()) {
            const int tier = itemJson["toolTier"].get<int>();
            def.toolTier = static_cast<uint8_t>(std::max(0, std::min(tier, 255)));
            def.isTool = true;
        }

        if (itemJson.contains("toolEfficiency") && itemJson["toolEfficiency"].is_number()) {
            const float efficiency = itemJson["toolEfficiency"].get<float>();
            def.toolEfficiency = std::max(0.1f, efficiency);
            def.isTool = true;
        }

        if (itemJson.contains("maxDurability") && itemJson["maxDurability"].is_number_integer()) {
            const int durability = itemJson["maxDurability"].get<int>();
            def.maxDurability = static_cast<uint16_t>(std::max(0, std::min(durability, 65535)));
        }

        std::vector<NamespacedId> parsedTags;
        std::vector<ItemUseRule> parsedUseRules;
        std::string parseError;
        if (!parseItemTagList(itemJson, def.namespacedId.full(), parsedTags, parseError) ||
            !parseUseOnBlockRules(itemJson, def.namespacedId.full(), parsedUseRules, parseError)) {
            std::cerr << parseError << '\n';
            s_initializing = false;
            return false;
        }
        appendUniqueTags(def.tags, parsedTags);
        def.useOnBlockRules = std::move(parsedUseRules);

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

    // Fix iconTextureName pointers — vector resizes during registerItem()
    // may have invalidated c_str() pointers stored in s_items[].iconTextureName.
    for (size_t i = 0; i < s_items.size(); ++i) {
        if (i < s_itemIconTextureNames.size()) {
            s_items[i].iconTextureName = s_itemIconTextureNames[i].c_str();
        }
    }

    s_initializing = false;
    s_initialized = true;
    return true;
}

const ItemDef& ItemRegistry::get(const ItemID id) {
    if (!s_initialized) {
        if (!init()) {
            std::cerr << "Item registry failed to initialize\n";
            std::abort();
        }
    }
    if (id >= s_items.size()) {
        return s_items[0]; // AIR
    }
    return s_items[id];
}

ItemID ItemRegistry::findByName(const std::string& name) {
    ItemID id = 0;
    if (!tryGetIdByName(name, id)) {
        std::cerr << "Item is not registered: " << name << '\n';
        std::abort();
    }
    return id;
}

ItemID ItemRegistry::requireIdByName(const std::string& name) {
    ItemID id = 0;
    if (!tryGetIdByName(name, id)) {
        std::cerr << "Required item is not registered: " << name << '\n';
        std::abort();
    }
    return id;
}

bool ItemRegistry::tryGetIdByName(const std::string& name, ItemID& outId) {
    if (!s_initialized && !s_initializing) {
        if (!init()) {
            return false;
        }
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
    if (!s_initialized && !init()) {
        std::cerr << "Item registry failed to initialize\n";
        std::abort();
    }
    auto it = s_idLookup.find(namespacedId);
    if (it != s_idLookup.end()) {
        return it->second;
    }
    std::cerr << "Item is not registered: " << namespacedId.full() << '\n';
    std::abort();
}

bool ItemRegistry::tryGetId(const NamespacedId& namespacedId, ItemID& outId) {
    if (!s_initialized && !init()) {
        return false;
    }
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
    return 0; // AIR
}

BlockID ItemRegistry::toPlaceBlock(const ItemID itemId) {
    auto it = s_itemToPlaceBlock.find(itemId);
    if (it != s_itemToPlaceBlock.end()) {
        return it->second;
    }
    return 0; // AIR
}

BlockID ItemRegistry::toRenderBlock(const ItemID itemId) {
    auto it = s_itemToRenderBlock.find(itemId);
    if (it != s_itemToRenderBlock.end()) {
        return it->second;
    }
    return 0; // AIR
}

bool ItemRegistry::hasTag(const ItemID itemId, const NamespacedId& tag) {
    const ItemDef& def = get(itemId);
    return std::find(def.tags.begin(), def.tags.end(), tag) != def.tags.end();
}

ItemID ItemRegistry::registerItem(const NamespacedId& id, ItemDef def) {
    auto it = s_idLookup.find(id);
    if (it != s_idLookup.end()) {
        return it->second;
    }

    ItemID runtimeId = s_idRegistry.registerId(id);
    def.namespacedId = id;

    const bool resized = (runtimeId >= s_items.size());
    if (resized) {
        s_items.resize(runtimeId + 1);
        s_itemIconTextureNames.resize(runtimeId + 1);
    }
    s_items[runtimeId] = def;
    s_idLookup[id] = runtimeId;

    // Fix iconTextureName pointers — resize may have invalidated c_str() pointers
    if (resized) {
        for (size_t i = 0; i < s_items.size(); ++i) {
            if (i < s_itemIconTextureNames.size()) {
                s_items[i].iconTextureName = s_itemIconTextureNames[i].c_str();
            }
        }
    }

    return runtimeId;
}

size_t ItemRegistry::getItemCount() {
    return s_items.size();
}

// ─── BlockDropTable ──────────────────────────────────────────────────────────

void BlockDropTable::init() {
    if (s_initialized)
        return;

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
    if (!s_initialized)
        init();
    auto it = s_drops.find(resolveDropBlockId(blockId));
    if (it == s_drops.end())
        return 0;
    return it->second.dropItem;
}

const BlockDropEntry& BlockDropTable::get(const BlockID blockId) {
    if (!s_initialized)
        init();
    auto it = s_drops.find(resolveDropBlockId(blockId));
    if (it == s_drops.end()) {
        static BlockDropEntry empty{0, 1, 1};
        return empty;
    }
    return it->second;
}
