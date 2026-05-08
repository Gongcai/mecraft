//
// Created by Caiwe on 2026/3/24.
//

#include "Block.h"
#include "BlockStateRegistry.h"
#include "FluidRegistry.h"
#include "Placement.h"
#include "PropIndices.h"
#include "Paths.h"
#include "../renderer/MeshBuilderRegistry.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <string>

#include "../resource/ResourceMgr.h"

IdRegistry BlockRegistry::s_idRegistry{};
std::vector<BlockDef> BlockRegistry::s_blocks{};
std::vector<NamespacedId> BlockRegistry::s_blockDropIds{};
std::unordered_map<NamespacedId, BlockID> BlockRegistry::s_idLookup{};
bool BlockRegistry::s_initialized = false;

namespace {
constexpr const char* kBlocksConfigPath = BLOCKS_CONFIG_PATH;

AnimatedTextureRef makeStaticWorldTexture(const int layer) {
    AnimatedTextureRef ref;
    ref.firstLayer = layer;
    ref.frameCount = 1;
    ref.fps = 0.0f;
    ref.isAnimated = false;
    return ref;
}

void setAllWorldFaces(BlockDef& def, const AnimatedTextureRef& ref) {
    def.worldTop = ref;
    def.worldBottom = ref;
    def.worldLeft = ref;
    def.worldRight = ref;
    def.worldFront = ref;
    def.worldBack = ref;
}

void setAllFaces(BlockDef& def, int tex) {
    def.texTop = tex;
    def.texBottom = tex;
    def.texLeft = tex;
    def.texRight = tex;
    def.texFront = tex;
    def.texBack = tex;
    setAllWorldFaces(def, makeStaticWorldTexture(std::max(0, tex)));
}

BlockID resolveDefinitionBlockId(const BlockID id) {
    if (id < BlockRegistry::getBlockCount()) {
        return id;
    }

    if (id < BlockStateRegistry::getStateCount()) {
        return BlockStateRegistry::getBlockId(id);
    }

    return BlockIds::AIR;
}

BiomeTintKind parseBiomeTintKind(const nlohmann::json& blockJson) {
    if (blockJson.contains("biomeTint") && blockJson["biomeTint"].is_string()) {
        const std::string tint = blockJson["biomeTint"].get<std::string>();
        if (tint == "grass") {
            return BiomeTintKind::Grass;
        }
        if (tint == "foliage") {
            return BiomeTintKind::Foliage;
        }
        return BiomeTintKind::None;
    }

    if (blockJson.contains("useBiomeTint") && blockJson["useBiomeTint"].is_boolean()) {
        return blockJson["useBiomeTint"].get<bool>() ? BiomeTintKind::Grass : BiomeTintKind::None;
    }
    if (blockJson.contains("useGrassTint") && blockJson["useGrassTint"].is_boolean()) {
        return blockJson["useGrassTint"].get<bool>() ? BiomeTintKind::Grass : BiomeTintKind::None;
    }
    return BiomeTintKind::None;
}

BlockRenderLayer parseRenderLayer(const nlohmann::json& blockJson, const bool isTransparent) {
    if (blockJson.contains("renderLayer") && blockJson["renderLayer"].is_string()) {
        const std::string layer = blockJson["renderLayer"].get<std::string>();
        if (layer == "opaque") {
            return BlockRenderLayer::Opaque;
        }
        if (layer == "cutout") {
            return BlockRenderLayer::Cutout;
        }
        if (layer == "transparent") {
            return BlockRenderLayer::Transparent;
        }
    }

    return isTransparent ? BlockRenderLayer::Transparent : BlockRenderLayer::Opaque;
}
}

// Initialize BlockIds constants
namespace BlockIds {
#define MECRAFT_DEFINE_BLOCK_ID(symbol, path) BlockID symbol = 0;
MECRAFT_FOR_EACH_BUILTIN_BLOCK(MECRAFT_DEFINE_BLOCK_ID)
#undef MECRAFT_DEFINE_BLOCK_ID

void init() {
#define MECRAFT_INIT_BLOCK_ID(symbol, path) symbol = BlockRegistry::getId(NamespacedId("minecraft", path));
    MECRAFT_FOR_EACH_BUILTIN_BLOCK(MECRAFT_INIT_BLOCK_ID)
#undef MECRAFT_INIT_BLOCK_ID
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
        s_blocks[i].allowsFluidCoexistence = false;
        s_blocks[i].renderShape = BlockRenderShape::Cube;
        s_blocks[i].renderLayer = BlockRenderLayer::Opaque;
        s_blocks[i].cutoutDistanceCull = true;
        s_blocks[i].renderShapeName = "cube";
        s_blocks[i].renderShapeTag = 0;
        s_blocks[i].placementStrategy = "simple";
        s_blocks[i].supportRule.clear();
        s_blocks[i].biomeTint = BiomeTintKind::None;
        s_blocks[i].lightLevel = 0;
        s_blocks[i].opacity = 15;
        setAllFaces(s_blocks[i], 0);
        s_blockDropIds[i] = NamespacedId("minecraft", "air");
    }

    // Override AIR defaults
    s_blocks[0].isSolid = false;
    s_blocks[0].isTransparent = true;
    s_blocks[0].renderLayer = BlockRenderLayer::Transparent;
    s_blocks[0].isSelectable = false;
    s_blocks[0].opacity = 0;
    setAllFaces(s_blocks[0], -1);

    // Build idLookup map
    s_idLookup.clear();
    for (size_t i = 0; i < s_idRegistry.size(); ++i) {
        s_idLookup[s_idRegistry.getNamespacedId(static_cast<BlockID>(i))] = static_cast<BlockID>(i);
    }

    // Step 3: Load config from JSON
    PlacementStrategyRegistry::initBuiltinStrategies();
    MeshBuilderRegistry::initBuiltinBuilders();

    std::ifstream file(kBlocksConfigPath);
    if (!file.is_open()) {
#ifdef MECRAFT_DEBUG
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
#ifdef MECRAFT_DEBUG
        std::cerr << "[BlockRegistry] Failed to parse blocks.json: " << e.what() << std::endl;
#endif
        s_initialized = true;
        BlockIds::init();
        return;
    }

    if (!root.contains("blocks") || !root["blocks"].is_array()) {
#ifdef MECRAFT_DEBUG
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
        def.renderShape = BlockRenderShape::Cube;
        def.renderLayer = BlockRenderLayer::Opaque;
        def.cutoutDistanceCull = true;
        def.renderShapeName = "cube";
        def.renderShapeTag = 0;
        def.placementStrategy = "simple";
        def.supportRule.clear();
        def.namedTextureAnimations.clear();

        if (blockJson.contains("isSolid") && blockJson["isSolid"].is_boolean()) {
            def.isSolid = blockJson["isSolid"].get<bool>();
        }
        if (blockJson.contains("isTransparent") && blockJson["isTransparent"].is_boolean()) {
            def.isTransparent = blockJson["isTransparent"].get<bool>();
        }
        def.renderLayer = parseRenderLayer(blockJson, def.isTransparent);
        if (blockJson.contains("cutoutDistanceCull") && blockJson["cutoutDistanceCull"].is_boolean()) {
            def.cutoutDistanceCull = blockJson["cutoutDistanceCull"].get<bool>();
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
            def.renderShapeName = blockJson["renderShape"].get<std::string>();
            def.renderShapeTag = MeshBuilderRegistry::getShapeTag(def.renderShapeName);
            if (def.renderShapeTag == MeshBuilderRegistry::INVALID_TAG) {
                def.renderShapeName = "cube";
                def.renderShapeTag = MeshBuilderRegistry::CUBE_TAG;
            }

            switch (MeshBuilderRegistry::getShapeClass(def.renderShapeTag)) {
                case MeshShapeClass::Cross:
                    def.renderShape = BlockRenderShape::Cross;
                    break;
                case MeshShapeClass::Custom:
                    def.renderShape = BlockRenderShape::Custom;
                    break;
                case MeshShapeClass::Cube:
                default:
                    def.renderShape = BlockRenderShape::Cube;
                    break;
            }
        }
        if (blockJson.contains("placementStrategy") && blockJson["placementStrategy"].is_string()) {
            def.placementStrategy = blockJson["placementStrategy"].get<std::string>();
        }
        if (blockJson.contains("supportRule") && blockJson["supportRule"].is_string()) {
            def.supportRule = blockJson["supportRule"].get<std::string>();
        }
        def.biomeTint = parseBiomeTintKind(blockJson);
        if (blockJson.contains("timeToBreak") && blockJson["timeToBreak"].is_number_integer()) {
            def.timeToBreak = blockJson["timeToBreak"].get<int>();
        }
        if (blockJson.contains("allowsFluidCoexistence") && blockJson["allowsFluidCoexistence"].is_boolean()) {
            def.allowsFluidCoexistence = blockJson["allowsFluidCoexistence"].get<bool>();
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

            auto resolveWorldTexture = [&](const char* key) -> AnimatedTextureRef {
#ifdef MECRAFT_NO_TEXTURES
                return makeStaticWorldTexture(0);
#else
                if (!tex.contains(key) || !tex[key].is_string() || resourceMgr == nullptr) {
                    return makeStaticWorldTexture(0);
                }
                const std::string name = tex[key].get<std::string>();
                return makeStaticWorldTexture(resourceMgr->getTextureArrayLayer(name));
#endif
            };

            if (tex.contains("all")) {
                const int idx = resolveTexName("all");
                setAllFaces(def, idx);
                setAllWorldFaces(def, resolveWorldTexture("all"));
            }
            if (tex.contains("top")) {
                def.texTop = resolveTexName("top");
                def.worldTop = resolveWorldTexture("top");
            }
            if (tex.contains("bottom")) {
                def.texBottom = resolveTexName("bottom");
                def.worldBottom = resolveWorldTexture("bottom");
            }
            if (tex.contains("side")) {
                const int idx = resolveTexName("side");
                def.texLeft  = idx;
                def.texRight = idx;
                def.texFront = idx;
                def.texBack  = idx;
                const AnimatedTextureRef ref = resolveWorldTexture("side");
                def.worldLeft = ref;
                def.worldRight = ref;
                def.worldFront = ref;
                def.worldBack = ref;
            }
            if (tex.contains("left")) {
                def.texLeft = resolveTexName("left");
                def.worldLeft = resolveWorldTexture("left");
            }
            if (tex.contains("right")) {
                def.texRight = resolveTexName("right");
                def.worldRight = resolveWorldTexture("right");
            }
            if (tex.contains("front")) {
                def.texFront = resolveTexName("front");
                def.worldFront = resolveWorldTexture("front");
            }
            if (tex.contains("back")) {
                def.texBack = resolveTexName("back");
                def.worldBack = resolveWorldTexture("back");
            }
        }

        if (blockJson.contains("animatedTextures") && blockJson["animatedTextures"].is_object()) {
            for (auto it = blockJson["animatedTextures"].begin(); it != blockJson["animatedTextures"].end(); ++it) {
                if (!it.value().is_object()) {
                    continue;
                }
                const auto textureIt = it.value().find("texture");
                const auto framesIt = it.value().find("frames");
                const auto fpsIt = it.value().find("fps");
                if (textureIt == it.value().end() || !textureIt->is_string() ||
                    framesIt == it.value().end() || !framesIt->is_number_integer() ||
                    fpsIt == it.value().end() || !fpsIt->is_number()) {
                    continue;
                }

                NamedTextureAnimation animation;
                animation.textureName = textureIt->get<std::string>();
                animation.ref.frameCount = static_cast<uint16_t>(std::max(1, framesIt->get<int>()));
                animation.ref.fps = std::max(0.0f, fpsIt->get<float>());
                animation.ref.isAnimated = animation.ref.frameCount > 1;

#ifndef MECRAFT_NO_TEXTURES
                if (resourceMgr != nullptr) {
                    const TextureAnimationInfo resolved = resourceMgr->getTextureAnimation(animation.textureName);
                    animation.ref.firstLayer = resolved.firstLayer;
                    if (resolved.isAnimated) {
                        animation.ref.frameCount = static_cast<uint16_t>(resolved.frameCount);
                        animation.ref.fps = resolved.fps;
                        animation.ref.isAnimated = true;
                    } else {
                        animation.ref.firstLayer = resourceMgr->getTextureArrayLayer(animation.textureName);
                    }
                }
#endif
                def.namedTextureAnimations[it.key()] = animation;
            }
        }

        if (blockJson.contains("drop") && blockJson["drop"].is_string()) {
            s_blockDropIds[id] = NamespacedId(blockJson["drop"].get<std::string>());
        }

        if (blockJson.contains("properties") && blockJson["properties"].is_object()) {
            std::vector<std::pair<std::string, std::vector<std::string>>> properties;
            std::map<std::string, std::string> defaultState;

            for (auto it = blockJson["properties"].begin(); it != blockJson["properties"].end(); ++it) {
                if (!it.value().is_array()) {
                    continue;
                }

                std::vector<std::string> values;
                values.reserve(it.value().size());
                for (const auto& rawValue : it.value()) {
                    if (rawValue.is_string()) {
                        values.push_back(rawValue.get<std::string>());
                    }
                }

                if (!values.empty()) {
                    properties.emplace_back(it.key(), std::move(values));
                }
            }

            if (blockJson.contains("defaultState") && blockJson["defaultState"].is_object()) {
                for (auto it = blockJson["defaultState"].begin(); it != blockJson["defaultState"].end(); ++it) {
                    if (it.value().is_string()) {
                        defaultState[it.key()] = it.value().get<std::string>();
                    }
                }
            }

            if (!properties.empty()) {
                BlockStateRegistry::registerBlockProperties(id, std::move(properties), std::move(defaultState));
            }
        }

        s_blocks[id] = def;
    }

    BlockStateRegistry::explodeAllStates();
    PropIndices::init();

    s_initialized = true;
    BlockIds::init();
    FluidRegistry::init();
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

const BlockDef& BlockRegistry::getFast(const BlockID id) {
    const BlockID resolvedId = resolveDefinitionBlockId(id);
    return resolvedId < s_blocks.size() ? s_blocks[resolvedId] : s_blocks[0];
}

uint8_t BlockRegistry::getOpacityFast(const BlockID id) {
    return getFast(id).opacity;
}

uint8_t BlockRegistry::getLightLevelFast(const BlockID id) {
    return getFast(id).lightLevel;
}

bool BlockRegistry::isLightSourceFast(const BlockID id) {
    return getFast(id).isLightSource;
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
    return s_idRegistry.getNamespacedId(resolveDefinitionBlockId(runtimeId));
}

void BlockRegistry::printAllBlocks() {
#ifdef MECRAFT_DEBUG
    for (size_t i = 0; i < s_blocks.size(); ++i) {
        std::cout << i << " → " << s_blocks[i].namespacedId.full() << std::endl;
    }
#endif
}

const NamespacedId& BlockRegistry::getBlockDropId(const BlockID id) {
    const BlockID resolvedId = resolveDefinitionBlockId(id);
    if (resolvedId >= s_blockDropIds.size()) {
        static const NamespacedId empty("minecraft", "air");
        return empty;
    }
    return s_blockDropIds[resolvedId];
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
