//
// Created by Caiwe on 2026/3/24.
//

#include "Block.h"
#include "Paths.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>

#include "../resource/ResourceMgr.h"

IdRegistry BlockRegistry::s_idRegistry{};
std::vector<BlockDef> BlockRegistry::s_blocks{};
std::vector<NamespacedId> BlockRegistry::s_blockDropIds{};
std::unordered_map<NamespacedId, BlockID> BlockRegistry::s_idLookup{};
bool BlockRegistry::s_initialized = false;

namespace {
constexpr const char* kBlocksConfigPath = BLOCKS_CONFIG_PATH;

void setAllFaces(BlockDef& def, int tex) {
    def.texTop = tex;
    def.texBottom = tex;
    def.texLeft = tex;
    def.texRight = tex;
    def.texFront = tex;
    def.texBack = tex;
}
}

// Initialize BlockIds constants
namespace BlockIds {
BlockID AIR = 0;
BlockID DIRT = 1;
BlockID GRASS = 2;
BlockID STONE = 3;
BlockID SAND = 4;
BlockID WOOD = 5;
BlockID GLASS = 6;
BlockID COAL_ORE = 7;
BlockID DIAMOND_ORE = 8;
BlockID GOLD_ORE = 9;
BlockID IRON_ORE = 10;
BlockID WATER = 11;
BlockID BEDROCK = 12;
BlockID TALL_GRASS = 13;
BlockID ROSE = 14;
BlockID OAK_PLANKS = 15;
BlockID SPRUCE_PLANKS = 16;
BlockID BIRCH_PLANKS = 17;
BlockID JUNGLE_PLANKS = 18;
BlockID ACACIA_PLANKS = 19;
BlockID DARK_OAK_PLANKS = 20;
BlockID MANGROVE_PLANKS = 21;
BlockID CHERRY_PLANKS = 22;
BlockID PALE_OAK_PLANKS = 23;
BlockID BAMBOO_PLANKS = 24;
BlockID CRIMSON_PLANKS = 25;
BlockID WARPED_PLANKS = 26;
BlockID BIRCH_LOG = 27;
BlockID TORCH = 28;
BlockID BROWN_MUSHROOM = 29;

void init() {
    /*AIR            = BlockRegistry::getId(NamespacedId("minecraft", "air"));
    DIRT           = BlockRegistry::getId(NamespacedId("minecraft", "dirt"));
    GRASS          = BlockRegistry::getId(NamespacedId("minecraft", "grass_block"));
    STONE          = BlockRegistry::getId(NamespacedId("minecraft", "stone"));
    SAND           = BlockRegistry::getId(NamespacedId("minecraft", "sand"));
    WOOD           = BlockRegistry::getId(NamespacedId("minecraft", "oak_log"));
    GLASS          = BlockRegistry::getId(NamespacedId("minecraft", "glass"));
    COAL_ORE       = BlockRegistry::getId(NamespacedId("minecraft", "coal_ore"));
    DIAMOND_ORE    = BlockRegistry::getId(NamespacedId("minecraft", "diamond_ore"));
    GOLD_ORE       = BlockRegistry::getId(NamespacedId("minecraft", "gold_ore"));
    IRON_ORE       = BlockRegistry::getId(NamespacedId("minecraft", "iron_ore"));
    WATER          = BlockRegistry::getId(NamespacedId("minecraft", "water"));
    BEDROCK        = BlockRegistry::getId(NamespacedId("minecraft", "bedrock"));
    TALL_GRASS     = BlockRegistry::getId(NamespacedId("minecraft", "tall_grass"));
    ROSE           = BlockRegistry::getId(NamespacedId("minecraft", "rose"));
    OAK_PLANKS     = BlockRegistry::getId(NamespacedId("minecraft", "oak_planks"));
    SPRUCE_PLANKS  = BlockRegistry::getId(NamespacedId("minecraft", "spruce_planks"));
    BIRCH_PLANKS   = BlockRegistry::getId(NamespacedId("minecraft", "birch_planks"));
    JUNGLE_PLANKS  = BlockRegistry::getId(NamespacedId("minecraft", "jungle_planks"));
    ACACIA_PLANKS  = BlockRegistry::getId(NamespacedId("minecraft", "acacia_planks"));
    DARK_OAK_PLANKS = BlockRegistry::getId(NamespacedId("minecraft", "dark_oak_planks"));
    MANGROVE_PLANKS = BlockRegistry::getId(NamespacedId("minecraft", "mangrove_planks"));
    CHERRY_PLANKS  = BlockRegistry::getId(NamespacedId("minecraft", "cherry_planks"));
    PALE_OAK_PLANKS = BlockRegistry::getId(NamespacedId("minecraft", "pale_oak_planks"));
    BAMBOO_PLANKS  = BlockRegistry::getId(NamespacedId("minecraft", "bamboo_planks"));
    CRIMSON_PLANKS = BlockRegistry::getId(NamespacedId("minecraft", "crimson_planks"));
    WARPED_PLANKS  = BlockRegistry::getId(NamespacedId("minecraft", "warped_planks"));
    BIRCH_LOG      = BlockRegistry::getId(NamespacedId("minecraft", "birch_log"));
    TORCH          = BlockRegistry::getId(NamespacedId("minecraft", "torch"));
    BROWN_MUSHROOM = BlockRegistry::getId(NamespacedId("minecraft", "brown_mushroom"));*/
}
}

void BlockRegistry::init(ResourceMgr* resourceMgr) {
    if (s_initialized) {
        return;
    }

    // Step 1: Register all built-in block IDs in stable order
    s_idRegistry.initBuiltinBlockIds();

    // Step 2: Create default BlockDef entries for each registered ID
    s_blocks.resize(s_idRegistry.size());
    s_blockDropIds.resize(s_idRegistry.size());

    for (size_t i = 0; i < s_blocks.size(); ++i) {
        s_blocks[i] = BlockDef{};
        s_blocks[i].namespacedId = s_idRegistry.getNamespacedId(static_cast<BlockID>(i));
        s_blocks[i].isSolid = true;
        s_blocks[i].isTransparent = false;
        s_blocks[i].isLightSource = false;
        s_blocks[i].renderShape = BlockRenderShape::Cube;
        s_blocks[i].useGrassTint = false;
        s_blocks[i].lightLevel = 0;
        s_blocks[i].opacity = 15;
        setAllFaces(s_blocks[i], 0);
        s_blockDropIds[i] = NamespacedId("minecraft", "air");
    }

    // Override AIR defaults
    s_blocks[0].isSolid = false;
    s_blocks[0].isTransparent = true;
    s_blocks[0].isSelectable = false;
    s_blocks[0].opacity = 0;
    setAllFaces(s_blocks[0], -1);

    // Build idLookup map
    s_idLookup.clear();
    for (size_t i = 0; i < s_idRegistry.size(); ++i) {
        s_idLookup[s_idRegistry.getNamespacedId(static_cast<BlockID>(i))] = static_cast<BlockID>(i);
    }

    // Step 3: Load config from JSON
    std::ifstream file(kBlocksConfigPath);
    if (!file.is_open()) {
#ifndef NDEBUG
        std::cerr << "[BlockRegistry] Failed to open config: " << kBlocksConfigPath << std::endl;
#endif
        s_initialized = true;
        BlockIds::init();
        return;
    }

    nlohmann::json root;
    try {
        file >> root;
    } catch (const std::exception& e) {
#ifndef NDEBUG
        std::cerr << "[BlockRegistry] Failed to parse blocks.json: " << e.what() << std::endl;
#endif
        s_initialized = true;
        BlockIds::init();
        return;
    }

    if (!root.contains("blocks") || !root["blocks"].is_array()) {
#ifndef NDEBUG
        std::cerr << "[BlockRegistry] Invalid blocks.json: missing 'blocks' array." << std::endl;
#endif
        s_initialized = true;
        BlockIds::init();
        return;
    }

    for (const auto& blockJson : root["blocks"]) {
        BlockID id = 0;
        bool found = false;

        if (blockJson.contains("id") && blockJson["id"].is_string()) {
            const std::string idStr = blockJson["id"].get<std::string>();
            NamespacedId nsId(idStr);
            auto it = s_idLookup.find(nsId);
            if (it != s_idLookup.end()) {
                id = it->second;
                found = true;
            } else {
                id = registerBlock(nsId, BlockDef{});
                found = true;
            }
        }

        if (!found) {
            continue;
        }

        // Ensure vectors are large enough
        if (id >= s_blocks.size()) {
            s_blocks.resize(id + 1);
            s_blockDropIds.resize(id + 1, NamespacedId("minecraft", "air"));
        }

        BlockDef def = s_blocks[id];
        def.namespacedId = s_idRegistry.getNamespacedId(id);

        if (blockJson.contains("isSolid") && blockJson["isSolid"].is_boolean()) {
            def.isSolid = blockJson["isSolid"].get<bool>();
        }
        if (blockJson.contains("isTransparent") && blockJson["isTransparent"].is_boolean()) {
            def.isTransparent = blockJson["isTransparent"].get<bool>();
        }
        if (blockJson.contains("isLightSource") && blockJson["isLightSource"].is_boolean()) {
            def.isLightSource = blockJson["isLightSource"].get<bool>();
        }
        if (blockJson.contains("lightLevel") && blockJson["lightLevel"].is_number_integer()) {
            const int light = blockJson["lightLevel"].get<int>();
            def.lightLevel = static_cast<uint8_t>(std::clamp(light, 0, 15));
        }
        if (blockJson.contains("isSelectable") && blockJson["isSelectable"].is_boolean()) {
            def.isSelectable = blockJson["isSelectable"].get<bool>();
        }
        if (blockJson.contains("opacity") && blockJson["opacity"].is_number_integer()) {
            const int o = blockJson["opacity"].get<int>();
            def.opacity = static_cast<uint8_t>(std::clamp(o, 0, 15));
        } else {
            def.opacity = def.isSolid ? 15 : 0;
        }
        if (blockJson.contains("renderShape") && blockJson["renderShape"].is_string()) {
            const std::string renderShape = blockJson["renderShape"].get<std::string>();
            def.renderShape = (renderShape == "cross") ? BlockRenderShape::Cross : BlockRenderShape::Cube;
        }
        if (blockJson.contains("useGrassTint") && blockJson["useGrassTint"].is_boolean()) {
            def.useGrassTint = blockJson["useGrassTint"].get<bool>();
        }
        if (blockJson.contains("timeToBreak") && blockJson["timeToBreak"].is_number_integer()) {
            def.timeToBreak = blockJson["timeToBreak"].get<int>();
        }

        if (blockJson.contains("textures") && blockJson["textures"].is_object()) {
            const auto& tex = blockJson["textures"];

            auto resolveTexName = [&](const char* key) -> int {
#ifdef MECRAFT_NO_TEXTURES
                return 0;
#else
                if (!tex.contains(key) || !tex[key].is_string() || resourceMgr == nullptr) {
                    return 0;
                }
                const std::string name = tex[key].get<std::string>();
                return static_cast<int>(resourceMgr->getTexture(name));
#endif
            };

            if (tex.contains("all")) {
                const int idx = resolveTexName("all");
                setAllFaces(def, idx);
            }
            if (tex.contains("top")) {
                def.texTop = resolveTexName("top");
            }
            if (tex.contains("bottom")) {
                def.texBottom = resolveTexName("bottom");
            }
            if (tex.contains("side")) {
                const int idx = resolveTexName("side");
                def.texLeft  = idx;
                def.texRight = idx;
                def.texFront = idx;
                def.texBack  = idx;
            }
            if (tex.contains("left")) {
                def.texLeft = resolveTexName("left");
            }
            if (tex.contains("right")) {
                def.texRight = resolveTexName("right");
            }
            if (tex.contains("front")) {
                def.texFront = resolveTexName("front");
            }
            if (tex.contains("back")) {
                def.texBack = resolveTexName("back");
            }
        }

        if (blockJson.contains("drop") && blockJson["drop"].is_string()) {
            s_blockDropIds[id] = NamespacedId(blockJson["drop"].get<std::string>());
        }

        s_blocks[id] = def;
    }

    s_initialized = true;
    BlockIds::init();
}

void BlockRegistry::ensureInitialized() {
    if (!s_initialized) {
        init(nullptr);
    }
}

const BlockDef& BlockRegistry::get(BlockID id) {
    ensureInitialized();
    return getFast(id);
}

const BlockDef& BlockRegistry::getFast(BlockID id) {
    if (id >= s_blocks.size()) {
        return s_blocks[0];  // Return AIR for invalid IDs
    }

    return s_blocks[id];
}

BlockID BlockRegistry::findByName(const std::string& name) {
    BlockID outId = 0;
    if (!tryGetIdByName(name, outId)) {
        return 0;  // AIR
    }
    return outId;
}

bool BlockRegistry::tryGetIdByName(const std::string& name, BlockID& outId) {
    ensureInitialized();

    // Try as NamespacedId first (contains ':')
    if (name.find(':') != std::string::npos) {
        NamespacedId nsId(name);
        return tryGetId(nsId, outId);
    }

    // Try as path with default "minecraft" namespace
    NamespacedId nsId("minecraft", name);
    return tryGetId(nsId, outId);
}

BlockID BlockRegistry::getId(const NamespacedId& namespacedId) {
    ensureInitialized();
    auto it = s_idLookup.find(namespacedId);
    if (it != s_idLookup.end()) {
        return it->second;
    }
    return 0;  // AIR
}

bool BlockRegistry::tryGetId(const NamespacedId& namespacedId, BlockID& outId) {
    ensureInitialized();
    auto it = s_idLookup.find(namespacedId);
    if (it != s_idLookup.end()) {
        outId = it->second;
        return true;
    }
    return false;
}

const NamespacedId& BlockRegistry::getNamespacedId(BlockID runtimeId) {
    return s_idRegistry.getNamespacedId(runtimeId);
}

void BlockRegistry::printAllBlocks() {
#ifndef NDEBUG
    for (size_t i = 0; i < s_blocks.size(); ++i) {
        std::cout << i << " → " << s_blocks[i].namespacedId.full() << std::endl;
    }
#endif
}

const NamespacedId& BlockRegistry::getBlockDropId(const BlockID id) {
    if (id >= s_blockDropIds.size()) {
        static const NamespacedId empty("minecraft", "air");
        return empty;
    }
    return s_blockDropIds[id];
}

BlockID BlockRegistry::registerBlock(const NamespacedId& id, BlockDef def) {
    // Check if already registered
    auto it = s_idLookup.find(id);
    if (it != s_idLookup.end()) {
        return it->second;
    }

    BlockID runtimeId = s_idRegistry.registerId(id);
    def.namespacedId = id;

    if (runtimeId >= s_blocks.size()) {
        s_blocks.resize(runtimeId + 1);
        s_blockDropIds.resize(runtimeId + 1, NamespacedId("minecraft", "air"));
    }
    s_blocks[runtimeId] = def;
    s_idLookup[id] = runtimeId;
    return runtimeId;
}

size_t BlockRegistry::getBlockCount() {
    return s_blocks.size();
}
