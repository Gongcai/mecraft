//
// Created by Caiwe on 2026/3/24.
//

#include "Block.h"
#include "../../Diagnostics.h"
#include "BlockModelRegistry.h"
#include "BlockStateRegistry.h"
#include "../fluid/FluidRegistry.h"
#include "Placement.h"
#include "PropIndices.h"
#include "Paths.h"
#include "../../renderer/mesh/MeshBuilderRegistry.h"

#include <algorithm>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>

#include "../../resource/ResourceMgr.h"

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

void setAllFaces(BlockDef& def, const AnimatedTextureRef& ref) {
    def.faceTop = ref;
    def.faceBottom = ref;
    def.faceLeft = ref;
    def.faceRight = ref;
    def.faceFront = ref;
    def.faceBack = ref;
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

BiomeTintKind biomeTintKindFromResourceTint(const ResourceTextureTint tint) {
    switch (tint) {
        case ResourceTextureTint::Grass:
            return BiomeTintKind::Grass;
        case ResourceTextureTint::Foliage:
            return BiomeTintKind::Foliage;
        case ResourceTextureTint::None:
        default:
            return BiomeTintKind::None;
    }
}

bool containsToken(const std::string_view value, const std::string_view token) {
    return value.find(token) != std::string_view::npos;
}

bool containsAnyToken(const std::string_view value, std::initializer_list<std::string_view> tokens) {
    for (const std::string_view token : tokens) {
        if (containsToken(value, token)) {
            return true;
        }
    }
    return false;
}

uint8_t inferBlockMaterialKind(const BlockDef& def) {
    const std::string_view path = def.namespacedId.path();
    if (def.isLightSource || def.lightLevel > 0 || containsAnyToken(path, {"torch", "lava", "glowstone", "sea_lantern", "lantern", "magma", "shroomlight"})) {
        return BlockMaterialKinds::EMISSIVE;
    }
    if (def.renderShape == BlockRenderShape::Cross || containsAnyToken(path, {"flower", "rose", "dandelion", "fern", "sapling", "bush"})) {
        return BlockMaterialKinds::PLANT;
    }
    if (def.renderLayer == BlockRenderLayer::Transparent) {
        if (containsToken(path, "water")) {
            return BlockMaterialKinds::WATER;
        }
        if (containsToken(path, "glass")) {
            return BlockMaterialKinds::GLASS;
        }
    }
    if (def.biomeTint == BiomeTintKind::Foliage || containsToken(path, "leaves")) {
        return BlockMaterialKinds::LEAVES;
    }
    if (path == "grass" || containsToken(path, "short_grass")) {
        return BlockMaterialKinds::GRASS;
    }
    if (containsAnyToken(path, {"ore", "diamond", "coal", "emerald", "lapis", "redstone", "amethyst", "quartz"})) {
        return BlockMaterialKinds::ORE;
    }
    if (containsAnyToken(path, {"iron_block", "gold_block", "copper_block", "cut_copper", "raw_iron_block", "raw_gold_block", "raw_copper_block"})) {
        return BlockMaterialKinds::METAL;
    }
    if (containsAnyToken(path, {"log", "wood", "planks", "stem", "hyphae", "bamboo"})) {
        return BlockMaterialKinds::WOOD;
    }
    if (containsAnyToken(path, {"sand", "gravel"})) {
        return BlockMaterialKinds::SAND;
    }
    if (containsAnyToken(path, {"dirt", "mud", "clay", "podzol", "mycelium"})) {
        return BlockMaterialKinds::DIRT;
    }
    if (containsAnyToken(path, {"stone", "bedrock", "deepslate", "andesite", "diorite", "granite", "tuff", "basalt", "brick", "concrete"})) {
        return BlockMaterialKinds::STONE;
    }
    return BlockMaterialKinds::DEFAULT;
}

uint8_t inferDerivativeMaterialId(const BlockDef& def) {
    const std::string_view path = def.namespacedId.path();

    if (containsToken(path, "water")) {
        return DerivativeMaterialIds::WATER;
    }
    if (containsToken(path, "stained_glass")) {
        return DerivativeMaterialIds::STAINED_GLASS;
    }
    if (containsToken(path, "glass")) {
        return DerivativeMaterialIds::STAINED_GLASS;
    }
    if (path == "ice") {
        return DerivativeMaterialIds::ICE;
    }
    if (path == "snow" || path == "snow_block" || path == "powder_snow" ||
        path == "frosted_ice" || path == "packed_ice" || path == "blue_ice") {
        return DerivativeMaterialIds::SNOW_ICE_SSS;
    }
    if (containsToken(path, "lava")) {
        return DerivativeMaterialIds::LAVA;
    }
    if (path == "fire" || containsToken(path, "lava_cauldron")) {
        return DerivativeMaterialIds::FIRE;
    }
    if (path == "torch" || path == "wall_torch" || path == "campfire" ||
        path == "lantern" || path == "furnace" || path == "blast_furnace" || path == "smoker") {
        return DerivativeMaterialIds::TORCH_LIKE;
    }
    if (path == "glowstone" || path == "magma_block" || path == "shroomlight" ||
        path == "redstone_lamp" || path == "jack_o_lantern" ||
        path == "crimson_stem" || path == "crimson_hyphae") {
        return DerivativeMaterialIds::GLOWSTONE_LIKE;
    }
    if (path == "sea_lantern" || path == "warped_stem" || path == "warped_hyphae" ||
        path == "redstone_wire") {
        return DerivativeMaterialIds::SEA_LANTERN_LIKE;
    }
    if (path == "redstone_torch" || path == "redstone_wall_torch" ||
        path == "repeater" || path == "comparator") {
        return DerivativeMaterialIds::REDSTONE;
    }
    if (path == "redstone_block" || containsToken(path, "powered_rail") ||
        containsToken(path, "activator_rail") || containsToken(path, "detector_rail") ||
        path == "observer") {
        return DerivativeMaterialIds::RAILS;
    }
    if (path == "soul_campfire" || path == "soul_torch" || path == "soul_wall_torch" ||
        path == "soul_lantern" || path == "soul_fire" || path == "sea_pickle" ||
        path == "dragon_head" || path == "spawner" || path == "enchanting_table" ||
        path == "respawn_anchor" || path == "crying_obsidian") {
        return DerivativeMaterialIds::SOUL_FIRE;
    }
    if (containsToken(path, "amethyst") || path == "calibrated_sculk_sensor") {
        return DerivativeMaterialIds::AMETHYST;
    }
    if (path == "cave_vines" || path == "cave_vines_plant") {
        return DerivativeMaterialIds::GLOWBERRY;
    }
    if (containsToken(path, "sculk")) {
        return DerivativeMaterialIds::SCULK;
    }
    if (path == "glow_lichen") {
        return DerivativeMaterialIds::GLOW_LICHEN;
    }
    if (path == "beacon") {
        return DerivativeMaterialIds::BEACON_CORE;
    }
    if (path == "end_portal" || path == "end_gateway") {
        return DerivativeMaterialIds::END_PORTAL;
    }
    if (containsToken(path, "froglight") || path == "end_rod" ||
        path == "lapis_block" || path == "emerald_block" || containsToken(path, "candle")) {
        return DerivativeMaterialIds::TOTAL_GLOWING;
    }
    if (containsToken(path, "weeping_vines") || path == "chorus_plant" ||
        path == "chorus_flower" || path == "crimson_fungus" || path == "warped_fungus") {
        return DerivativeMaterialIds::PARTIAL_GLOWING;
    }
    if (path == "brewing_stand" || containsToken(path, "candle_cake")) {
        return DerivativeMaterialIds::MIDDLE_GLOWING;
    }
    if (path == "nether_gold_ore" || path == "nether_quartz_ore") {
        return DerivativeMaterialIds::NETHER_ORE;
    }
    if (path == "iron_ore" || path == "deepslate_iron_ore" ||
        path == "copper_ore" || path == "deepslate_copper_ore" ||
        path == "gold_ore" || path == "deepslate_gold_ore" ||
        path == "redstone_ore" || path == "deepslate_redstone_ore" ||
        path == "lapis_ore" || path == "deepslate_lapis_ore" ||
        path == "emerald_ore" || path == "deepslate_emerald_ore" ||
        path == "diamond_ore" || path == "deepslate_diamond_ore" ||
        path == "gilded_blackstone") {
        return DerivativeMaterialIds::ORE;
    }
    if (def.biomeTint == BiomeTintKind::Foliage || containsToken(path, "leaves") ||
        path == "vine" || containsToken(path, "_vine")) {
        return DerivativeMaterialIds::LEAVES;
    }
    if (path == "wheat" || path == "carrots" || path == "potatoes" ||
        path == "beetroots" || path == "seagrass") {
        return DerivativeMaterialIds::WHEAT;
    }
    if (containsAnyToken(path, {"dandelion", "poppy", "orchid", "allium", "bluet", "tulip", "daisy", "cornflower", "rose", "torchflower"})) {
        return DerivativeMaterialIds::FLOWER;
    }
    if (path == "tall_grass" || path == "large_fern" || path == "sunflower" ||
        path == "lilac" || path == "rose_bush" || path == "peony" ||
        path == "tall_seagrass" || path == "pitcher_plant") {
        return DerivativeMaterialIds::GRASS_LOWER;
    }
    if (path == "fern" || path == "grass" || path == "short_grass") {
        return DerivativeMaterialIds::GRASS;
    }
    if (containsAnyToken(path, {"sapling", "dead_bush", "deadbush", "bamboo", "sugar_cane", "mangrove_roots", "cactus", "pumpkin_stem", "melon_stem", "kelp", "spore_blossom", "dripleaf", "pink_petals", "coral_block", "azalea"})) {
        return DerivativeMaterialIds::GRASS_LIKE;
    }

    return DerivativeMaterialIds::DEFAULT;
}

bool parseMaterialKind(const nlohmann::json& blockJson, uint8_t& outKind) {
    const auto materialIt = blockJson.find("materialKind");
    if (materialIt == blockJson.end()) {
        return false;
    }

    if (materialIt->is_number_integer()) {
        const int materialKind = materialIt->get<int>();
        outKind = static_cast<uint8_t>(std::clamp(materialKind, 0, static_cast<int>(BlockMaterialKinds::MAX_BUILTIN)));
        return true;
    }

    if (materialIt->is_string()) {
        const std::string materialName = materialIt->get<std::string>();
        return BlockMaterials::tryParseKindName(materialName, outKind);
    }

    return false;
}

bool parseDerivativeMaterialId(const nlohmann::json& blockJson, uint8_t& outId) {
    const auto materialIt = blockJson.find("derivativeMaterialId");
    if (materialIt == blockJson.end()) {
        return false;
    }
    if (!materialIt->is_number_integer()) {
        return false;
    }
    const int materialId = materialIt->get<int>();
    outId = static_cast<uint8_t>(std::clamp(materialId, 0, static_cast<int>(DerivativeMaterialIds::MAX_BUILTIN)));
    return true;
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
#define MECRAFT_DEFINE_BLOCK_ID(symbol, path) MECRAFT_API BlockID symbol = 0;
MECRAFT_FOR_EACH_BUILTIN_BLOCK(MECRAFT_DEFINE_BLOCK_ID)
#undef MECRAFT_DEFINE_BLOCK_ID

MECRAFT_API void init() {
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
        s_blocks[i].affectedByGravity = false;
        s_blocks[i].renderShape = BlockRenderShape::Cube;
        s_blocks[i].renderLayer = BlockRenderLayer::Opaque;
        s_blocks[i].cutoutDistanceCull = true;
        s_blocks[i].renderShapeName = "cube";
        s_blocks[i].renderShapeTag = 0;
        s_blocks[i].materialKind = BlockMaterialKinds::DEFAULT;
        s_blocks[i].derivativeMaterialId = DerivativeMaterialIds::DEFAULT;
        s_blocks[i].placementStrategy = "simple";
        s_blocks[i].revertPlacementFacing = false;
        s_blocks[i].supportRule.clear();
        s_blocks[i].biomeTint = BiomeTintKind::None;
        s_blocks[i].lightLevel = 0;
        s_blocks[i].opacity = 15;
        setAllFaces(s_blocks[i], makeStaticWorldTexture(0));
        s_blockDropIds[i] = NamespacedId("minecraft", "air");
    }

    // Override AIR defaults
    s_blocks[0].isSolid = false;
    s_blocks[0].isTransparent = true;
    s_blocks[0].renderLayer = BlockRenderLayer::Transparent;
    s_blocks[0].isSelectable = false;
    s_blocks[0].opacity = 0;
    setAllFaces(s_blocks[0], makeStaticWorldTexture(0));

    // Build idLookup map
    s_idLookup.clear();
    for (size_t i = 0; i < s_idRegistry.size(); ++i) {
        s_idLookup[s_idRegistry.getNamespacedId(static_cast<BlockID>(i))] = static_cast<BlockID>(i);
    }

    // Step 3: Load config from JSON
    PlacementStrategyRegistry::initBuiltinStrategies();
    MeshBuilderRegistry::initBuiltinBuilders();
    BlockModelRegistry::init(resourceMgr);

    std::ifstream file(kBlocksConfigPath);
    if (!file.is_open()) {
#ifdef MECRAFT_DEBUG
        MECRAFT_LOG_STREAM(std::cerr << "[BlockRegistry] Failed to open config: " << kBlocksConfigPath << std::endl);
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
        MECRAFT_LOG_STREAM(std::cerr << "[BlockRegistry] Failed to parse blocks.json: " << e.what() << std::endl);
#endif
        s_initialized = true;
        BlockIds::init();
        return;
    }

    if (!root.contains("blocks") || !root["blocks"].is_array()) {
#ifdef MECRAFT_DEBUG
        MECRAFT_LOG_STREAM(std::cerr << "[BlockRegistry] Invalid blocks.json: missing 'blocks' array." << std::endl);
#endif
        s_initialized = true;
        BlockIds::init();
        return;
    }

    std::vector<std::pair<BlockID, nlohmann::json>> pendingModelVariants;

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
        def.materialKind = BlockMaterialKinds::DEFAULT;
        def.derivativeMaterialId = DerivativeMaterialIds::DEFAULT;
        def.placementStrategy = "simple";
        def.revertPlacementFacing = false;
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
        if (blockJson.contains("revert") && blockJson["revert"].is_boolean()) {
            def.revertPlacementFacing = blockJson["revert"].get<bool>();
        }
        if (blockJson.contains("supportRule") && blockJson["supportRule"].is_string()) {
            def.supportRule = blockJson["supportRule"].get<std::string>();
        }
        const bool hasExplicitBiomeTint =
            blockJson.contains("biomeTint") ||
            blockJson.contains("useBiomeTint") ||
            blockJson.contains("useGrassTint");
        def.biomeTint = parseBiomeTintKind(blockJson);
        if (blockJson.contains("timeToBreak") && blockJson["timeToBreak"].is_number_integer()) {
            def.timeToBreak = blockJson["timeToBreak"].get<int>();
        }
        if (blockJson.contains("allowsFluidCoexistence") && blockJson["allowsFluidCoexistence"].is_boolean()) {
            def.allowsFluidCoexistence = blockJson["allowsFluidCoexistence"].get<bool>();
        }
        if (blockJson.contains("affectedByGravity") && blockJson["affectedByGravity"].is_boolean()) {
            def.affectedByGravity = blockJson["affectedByGravity"].get<bool>();
        }
        const bool hasExplicitMaterialKind = parseMaterialKind(blockJson, def.materialKind);
        const bool hasExplicitDerivativeMaterialId = parseDerivativeMaterialId(blockJson, def.derivativeMaterialId);

        bool hasTintedTexture = false;
        bool hasUntintedTexture = false;
        BiomeTintKind textureTint = BiomeTintKind::None;

        if (blockJson.contains("textures") && blockJson["textures"].is_object()) {
            const auto& tex = blockJson["textures"];

            auto resolveTexture = [&](const char* key) -> AnimatedTextureRef {
#ifdef MECRAFT_NO_TEXTURES
                return makeStaticWorldTexture(0);
#else
                if (!tex.contains(key) || resourceMgr == nullptr) {
                    return makeStaticWorldTexture(0);
                }
                if (!tex[key].is_string()) {
                    throw std::runtime_error(std::string("Block texture key must be a string: ") +
                                             def.namespacedId.full() + "." + key);
                }
                const std::string name = tex[key].get<std::string>();
                const BiomeTintKind resolvedTint = biomeTintKindFromResourceTint(resourceMgr->getTextureTint(name));
                if (resolvedTint == BiomeTintKind::None) {
                    hasUntintedTexture = true;
                } else {
                    hasTintedTexture = true;
                    if (textureTint == BiomeTintKind::None) {
                        textureTint = resolvedTint;
                    } else if (textureTint != resolvedTint) {
                        hasUntintedTexture = true;
                    }
                }
                return makeStaticWorldTexture(resourceMgr->getTextureArrayLayer(name));
#endif
            };

            if (tex.contains("all")) {
                setAllFaces(def, resolveTexture("all"));
            }
            if (tex.contains("top")) {
                def.faceTop = resolveTexture("top");
            }
            if (tex.contains("bottom")) {
                def.faceBottom = resolveTexture("bottom");
            }
            if (tex.contains("side")) {
                const AnimatedTextureRef ref = resolveTexture("side");
                def.faceLeft = ref;
                def.faceRight = ref;
                def.faceFront = ref;
                def.faceBack = ref;
            }
            if (tex.contains("left")) {
                def.faceLeft = resolveTexture("left");
            }
            if (tex.contains("right")) {
                def.faceRight = resolveTexture("right");
            }
            if (tex.contains("front")) {
                def.faceFront = resolveTexture("front");
            }
            if (tex.contains("back")) {
                def.faceBack = resolveTexture("back");
            }
        }

        if (!hasExplicitBiomeTint && hasTintedTexture && !hasUntintedTexture) {
            def.biomeTint = textureTint;
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
                    if (!resolved.isAnimated) {
                        throw std::runtime_error("Block animated texture is not declared as animated: " +
                                                 def.namespacedId.full() + "." + it.key());
                    }
                    animation.ref.firstLayer = resolved.firstLayer;
                    animation.ref.frameCount = static_cast<uint16_t>(resolved.frameCount);
                    animation.ref.fps = resolved.fps;
                    animation.ref.isAnimated = true;
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

        if (blockJson.contains("modelVariants")) {
            pendingModelVariants.emplace_back(id, blockJson["modelVariants"]);
        }

        if (!hasExplicitMaterialKind) {
            def.materialKind = inferBlockMaterialKind(def);
        }
        if (!hasExplicitDerivativeMaterialId) {
            def.derivativeMaterialId = inferDerivativeMaterialId(def);
        }

        s_blocks[id] = def;
    }

    BlockStateRegistry::explodeAllStates();
    for (const auto& [blockId, variantsJson] : pendingModelVariants) {
        BlockStateRegistry::registerBlockModelVariants(blockId, variantsJson);
    }
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
        MECRAFT_LOG_STREAM(std::cout << i << " → " << s_blocks[i].namespacedId.full() << std::endl);
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
