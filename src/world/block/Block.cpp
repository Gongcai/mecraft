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

bool isKnownRedstoneBehavior(const std::string_view behavior) {
    constexpr std::string_view kKnownBehaviors[] = {
        "",
        "wire",
        "torch",
        "repeater",
        "comparator",
        "piston",
        "observer",
        "lever",
        "button",
        "plate",
        "lamp",
        "door",
        "trapdoor",
        "fence_gate",
        "power_block",
        "target",
        "dispenser",
        "dropper",
        "hopper",
        "note_block",
    };
    return std::find(std::begin(kKnownBehaviors), std::end(kKnownBehaviors), behavior) != std::end(kKnownBehaviors);
}

bool isKnownPistonPushReaction(const std::string_view reaction) {
    return reaction == "normal" || reaction == "block";
}

bool isKnownPressurePlateEntityFilter(const std::string_view filter) {
    return filter == "living" || filter == "all";
}

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

BlockDef makeDefaultBlockDef(const NamespacedId& id) {
    BlockDef def{};
    def.namespacedId = id;
    def.isSolid = true;
    def.isTransparent = false;
    def.isLightSource = false;
    def.isSelectable = true;
    def.allowsFluidCoexistence = false;
    def.affectedByGravity = false;
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
    def.containerUi.clear();
    def.interaction.clear();
    def.tags.clear();
    def.biomeTint = BiomeTintKind::None;
    def.lightLevel = 0;
    def.opacity = 15;
    def.isRedstoneConductor = true;
    def.isRedstonePowerSource = false;
    def.respondsToRedstone = false;
    def.redstonePowerOutput = 0;
    def.redstonePulseTicks = 0;
    def.redstoneBehavior.clear();
    def.redstoneWireChannel.clear();
    def.redstoneWireChannelId = 0;
    def.redstoneWireTint = 0;
    def.pressurePlateEntityFilter.clear();
    def.redstoneControlledProperty.clear();
    def.redstoneControlledMirrorProperties.clear();
    def.redstoneControlledPowerInverted = false;
    def.pistonPushReaction = "normal";
    setAllFaces(def, makeStaticWorldTexture(0));
    return def;
}

BlockTextureFaces textureFacesFromBlock(const BlockDef& def) {
    BlockTextureFaces faces;
    faces.faceTop = def.faceTop;
    faces.faceBottom = def.faceBottom;
    faces.faceLeft = def.faceLeft;
    faces.faceRight = def.faceRight;
    faces.faceFront = def.faceFront;
    faces.faceBack = def.faceBack;
    return faces;
}

void setAllFaces(BlockTextureFaces& faces, const AnimatedTextureRef& ref) {
    faces.faceTop = ref;
    faces.faceBottom = ref;
    faces.faceLeft = ref;
    faces.faceRight = ref;
    faces.faceFront = ref;
    faces.faceBack = ref;
}

BlockID resolveDefinitionBlockId(const BlockID id) {
    if (id < BlockRegistry::getBlockCount()) {
        return id;
    }

    if (id < BlockStateRegistry::getStateCount()) {
        return BlockStateRegistry::getBlockId(id);
    }

    return RUNTIME_ID_NULL;
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

std::vector<NamespacedId> parseTagList(const nlohmann::json& ownerJson, const std::string& ownerName) {
    std::vector<NamespacedId> tags;
    const auto tagsIt = ownerJson.find("tags");
    if (tagsIt == ownerJson.end()) {
        return tags;
    }
    if (!tagsIt->is_array()) {
        throw std::runtime_error("Block tags must be an array: " + ownerName);
    }

    for (const nlohmann::json& tagJson : *tagsIt) {
        if (!tagJson.is_string()) {
            throw std::runtime_error("Block tag entries must be strings: " + ownerName);
        }
        tags.emplace_back(tagJson.get<std::string>());
    }
    return tags;
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

void BlockRegistry::init(ResourceMgr* resourceMgr) {
    if (s_initialized) {
        return;
    }

    // Load config from JSON before registering blocks so RuntimeId order follows the data file.
    PlacementStrategyRegistry::initBuiltinStrategies();
    MeshBuilderRegistry::initBuiltinBuilders();
    BlockModelRegistry::init(resourceMgr);

    std::ifstream file(kBlocksConfigPath);
    if (!file.is_open()) {
        throw std::runtime_error(std::string("Failed to open blocks config: ") + kBlocksConfigPath);
    }

    nlohmann::json root;
    try {
        file >> root;
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to parse blocks config: ") + e.what());
    }

    if (!root.contains("blocks") || !root["blocks"].is_array()) {
        throw std::runtime_error("blocks.json requires a blocks array");
    }

    s_idLookup.clear();
    std::unordered_map<std::string, uint16_t> redstoneWireChannelIds;
    auto assignRedstoneWireChannelId = [&](const std::string& channel, const std::string& blockName) {
        const auto existing = redstoneWireChannelIds.find(channel);
        if (existing != redstoneWireChannelIds.end()) {
            return existing->second;
        }
        if (redstoneWireChannelIds.size() >= 65535u) {
            throw std::runtime_error("Too many redstone wire channels while registering block: " + blockName);
        }
        const uint16_t channelId = static_cast<uint16_t>(redstoneWireChannelIds.size() + 1u);
        redstoneWireChannelIds.emplace(channel, channelId);
        return channelId;
    };

    for (const auto& blockJson : root["blocks"]) {
        if (!blockJson.contains("id") || !blockJson["id"].is_string()) {
            continue;
        }
        const NamespacedId nsId(blockJson["id"].get<std::string>());
        registerBlock(nsId, makeDefaultBlockDef(nsId));
    }

    const NamespacedId airId("minecraft", "air");
    if (s_blocks.empty() || s_blocks[0].namespacedId != airId) {
        throw std::runtime_error("blocks.json must register minecraft:air as RuntimeId 0.");
    }

    s_blocks[0].isSolid = false;
    s_blocks[0].isTransparent = true;
    s_blocks[0].renderLayer = BlockRenderLayer::Transparent;
    s_blocks[0].isSelectable = false;
    s_blocks[0].opacity = 0;
    s_blocks[0].isRedstoneConductor = false;
    setAllFaces(s_blocks[0], makeStaticWorldTexture(0));

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
        def.containerUi.clear();
        def.interaction.clear();
        def.tags.clear();
        def.namedTextureRefs.clear();
        def.namedTextureAnimations.clear();
        def.stateTextureRules.clear();
        def.isRedstoneConductor = false;
        def.isRedstonePowerSource = false;
        def.respondsToRedstone = false;
        def.redstonePowerOutput = 0;
        def.redstonePulseTicks = 0;
        def.redstoneBehavior.clear();
        def.redstoneWireChannel.clear();
        def.redstoneWireChannelId = 0;
        def.redstoneWireTint = 0;
        def.pressurePlateEntityFilter.clear();
        def.redstoneControlledProperty.clear();
        def.redstoneControlledMirrorProperties.clear();
        def.redstoneControlledPowerInverted = false;
        def.pistonPushReaction = "normal";

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
        if (blockJson.contains("containerUi")) {
            if (!blockJson["containerUi"].is_string()) {
                throw std::runtime_error("containerUi must be a string for block: " + def.namespacedId.full());
            }
            const NamespacedId containerUi(blockJson["containerUi"].get<std::string>());
            if (containerUi.namespaceStr().empty() || containerUi.path().empty()) {
                throw std::runtime_error("containerUi must not be empty for block: " + def.namespacedId.full());
            }
            def.containerUi = containerUi.full();
        }
        if (blockJson.contains("interaction")) {
            if (!blockJson["interaction"].is_string()) {
                throw std::runtime_error("interaction must be a string for block: " + def.namespacedId.full());
            }
            const NamespacedId interaction(blockJson["interaction"].get<std::string>());
            if (interaction.namespaceStr().empty() || interaction.path().empty()) {
                throw std::runtime_error("interaction must not be empty for block: " + def.namespacedId.full());
            }
            def.interaction = interaction.full();
        }
        def.tags = parseTagList(blockJson, def.namespacedId.full());
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
        const bool hasExplicitRedstoneConductor = blockJson.contains("isRedstoneConductor");
        if (hasExplicitRedstoneConductor) {
            if (!blockJson["isRedstoneConductor"].is_boolean()) {
                throw std::runtime_error("isRedstoneConductor must be a boolean for block: " + def.namespacedId.full());
            }
            def.isRedstoneConductor = blockJson["isRedstoneConductor"].get<bool>();
        } else {
            def.isRedstoneConductor = def.isSolid && def.renderShape == BlockRenderShape::Cube;
        }
        if (blockJson.contains("isRedstonePowerSource")) {
            if (!blockJson["isRedstonePowerSource"].is_boolean()) {
                throw std::runtime_error("isRedstonePowerSource must be a boolean for block: " + def.namespacedId.full());
            }
            def.isRedstonePowerSource = blockJson["isRedstonePowerSource"].get<bool>();
        }
        if (blockJson.contains("respondsToRedstone")) {
            if (!blockJson["respondsToRedstone"].is_boolean()) {
                throw std::runtime_error("respondsToRedstone must be a boolean for block: " + def.namespacedId.full());
            }
            def.respondsToRedstone = blockJson["respondsToRedstone"].get<bool>();
        }
        if (blockJson.contains("redstonePowerOutput")) {
            if (!blockJson["redstonePowerOutput"].is_number_integer()) {
                throw std::runtime_error("redstonePowerOutput must be an integer for block: " + def.namespacedId.full());
            }
            const int powerOutput = blockJson["redstonePowerOutput"].get<int>();
            if (powerOutput < 0 || powerOutput > 15) {
                throw std::runtime_error("redstonePowerOutput must be between 0 and 15 for block: " + def.namespacedId.full());
            }
            def.redstonePowerOutput = static_cast<uint8_t>(powerOutput);
        }
        if (blockJson.contains("redstonePulseTicks")) {
            if (!blockJson["redstonePulseTicks"].is_number_integer()) {
                throw std::runtime_error("redstonePulseTicks must be an integer for block: " + def.namespacedId.full());
            }
            const int pulseTicks = blockJson["redstonePulseTicks"].get<int>();
            if (pulseTicks <= 0) {
                throw std::runtime_error("redstonePulseTicks must be positive for block: " + def.namespacedId.full());
            }
            def.redstonePulseTicks = static_cast<uint64_t>(pulseTicks);
        }
        if (blockJson.contains("redstoneBehavior")) {
            if (!blockJson["redstoneBehavior"].is_string()) {
                throw std::runtime_error("redstoneBehavior must be a string for block: " + def.namespacedId.full());
            }
            def.redstoneBehavior = blockJson["redstoneBehavior"].get<std::string>();
            if (!isKnownRedstoneBehavior(def.redstoneBehavior)) {
                throw std::runtime_error("Unknown redstoneBehavior '" + def.redstoneBehavior + "' for block: " + def.namespacedId.full());
            }
        }
        if (def.redstoneBehavior == "button" && def.redstonePulseTicks == 0) {
            throw std::runtime_error("redstoneBehavior=button requires redstonePulseTicks for block: " +
                                     def.namespacedId.full());
        }
        if (blockJson.contains("redstoneWireChannel")) {
            if (!blockJson["redstoneWireChannel"].is_string()) {
                throw std::runtime_error("redstoneWireChannel must be a string for block: " +
                                         def.namespacedId.full());
            }
            if (def.redstoneBehavior != "wire") {
                throw std::runtime_error("redstoneWireChannel requires redstoneBehavior=wire for block: " +
                                         def.namespacedId.full());
            }
            def.redstoneWireChannel = blockJson["redstoneWireChannel"].get<std::string>();
            if (def.redstoneWireChannel.empty()) {
                throw std::runtime_error("redstoneWireChannel must not be empty for block: " +
                                         def.namespacedId.full());
            }
            def.redstoneWireChannelId =
                assignRedstoneWireChannelId(def.redstoneWireChannel, def.namespacedId.full());
        }
        if (blockJson.contains("redstoneWireTint")) {
            if (!blockJson["redstoneWireTint"].is_number_integer()) {
                throw std::runtime_error("redstoneWireTint must be an integer for block: " +
                                         def.namespacedId.full());
            }
            if (def.redstoneBehavior != "wire") {
                throw std::runtime_error("redstoneWireTint requires redstoneBehavior=wire for block: " +
                                         def.namespacedId.full());
            }
            const int tint = blockJson["redstoneWireTint"].get<int>();
            if (tint < 0 || tint > 15) {
                throw std::runtime_error("redstoneWireTint must be between 0 and 15 for block: " +
                                         def.namespacedId.full());
            }
            def.redstoneWireTint = static_cast<uint8_t>(tint);
        }
        if (def.redstoneBehavior == "wire" && def.redstoneWireChannel.empty()) {
            throw std::runtime_error("redstoneBehavior=wire requires redstoneWireChannel for block: " +
                                     def.namespacedId.full());
        }
        if (def.redstoneBehavior == "wire" && !blockJson.contains("redstoneWireTint")) {
            throw std::runtime_error("redstoneBehavior=wire requires redstoneWireTint for block: " +
                                     def.namespacedId.full());
        }
        if (def.redstoneBehavior == "wire" && def.redstoneWireChannelId == 0) {
            throw std::runtime_error("redstoneBehavior=wire requires a parsed redstoneWireChannelId for block: " +
                                     def.namespacedId.full());
        }
        if (blockJson.contains("pressurePlateEntityFilter")) {
            if (!blockJson["pressurePlateEntityFilter"].is_string()) {
                throw std::runtime_error("pressurePlateEntityFilter must be a string for block: " +
                                         def.namespacedId.full());
            }
            if (def.redstoneBehavior != "plate") {
                throw std::runtime_error("pressurePlateEntityFilter requires redstoneBehavior=plate for block: " +
                                         def.namespacedId.full());
            }
            def.pressurePlateEntityFilter = blockJson["pressurePlateEntityFilter"].get<std::string>();
            if (!isKnownPressurePlateEntityFilter(def.pressurePlateEntityFilter)) {
                throw std::runtime_error("Unknown pressurePlateEntityFilter '" + def.pressurePlateEntityFilter +
                                         "' for block: " + def.namespacedId.full());
            }
        }
        if (def.redstoneBehavior == "plate" && def.pressurePlateEntityFilter.empty()) {
            throw std::runtime_error("redstoneBehavior=plate requires pressurePlateEntityFilter for block: " +
                                     def.namespacedId.full());
        }
        if (blockJson.contains("pistonPushReaction")) {
            if (!blockJson["pistonPushReaction"].is_string()) {
                throw std::runtime_error("pistonPushReaction must be a string for block: " +
                                         def.namespacedId.full());
            }
            def.pistonPushReaction = blockJson["pistonPushReaction"].get<std::string>();
            if (!isKnownPistonPushReaction(def.pistonPushReaction)) {
                throw std::runtime_error("Unknown pistonPushReaction '" + def.pistonPushReaction +
                                         "' for block: " + def.namespacedId.full());
            }
        }
        if (blockJson.contains("redstoneControlledProperty")) {
            if (!blockJson["redstoneControlledProperty"].is_string()) {
                throw std::runtime_error("redstoneControlledProperty must be a string for block: " + def.namespacedId.full());
            }
            def.redstoneControlledProperty = blockJson["redstoneControlledProperty"].get<std::string>();
            if (def.redstoneControlledProperty.empty()) {
                throw std::runtime_error("redstoneControlledProperty must not be empty for block: " + def.namespacedId.full());
            }
            if (!def.respondsToRedstone) {
                throw std::runtime_error("redstoneControlledProperty requires respondsToRedstone=true for block: " + def.namespacedId.full());
            }
        }
        if (blockJson.contains("redstoneControlledMirrorProperties")) {
            if (!blockJson["redstoneControlledMirrorProperties"].is_array()) {
                throw std::runtime_error("redstoneControlledMirrorProperties must be an array for block: " +
                                         def.namespacedId.full());
            }
            if (def.redstoneControlledProperty.empty()) {
                throw std::runtime_error("redstoneControlledMirrorProperties requires redstoneControlledProperty for block: " +
                                         def.namespacedId.full());
            }
            for (const nlohmann::json& mirrorJson : blockJson["redstoneControlledMirrorProperties"]) {
                if (!mirrorJson.is_string()) {
                    throw std::runtime_error("redstoneControlledMirrorProperties entries must be strings for block: " +
                                             def.namespacedId.full());
                }
                const std::string mirrorProperty = mirrorJson.get<std::string>();
                if (mirrorProperty.empty()) {
                    throw std::runtime_error("redstoneControlledMirrorProperties entries must not be empty for block: " +
                                             def.namespacedId.full());
                }
                if (mirrorProperty == def.redstoneControlledProperty) {
                    throw std::runtime_error("redstoneControlledMirrorProperties must not repeat redstoneControlledProperty for block: " +
                                             def.namespacedId.full());
                }
                if (std::find(def.redstoneControlledMirrorProperties.begin(),
                              def.redstoneControlledMirrorProperties.end(),
                              mirrorProperty) != def.redstoneControlledMirrorProperties.end()) {
                    throw std::runtime_error("redstoneControlledMirrorProperties must not contain duplicates for block: " +
                                             def.namespacedId.full());
                }
                def.redstoneControlledMirrorProperties.push_back(mirrorProperty);
            }
        }
        if (blockJson.contains("redstoneControlledPowerInverted")) {
            if (!blockJson["redstoneControlledPowerInverted"].is_boolean()) {
                throw std::runtime_error("redstoneControlledPowerInverted must be a boolean for block: " +
                                         def.namespacedId.full());
            }
            if (def.redstoneControlledProperty.empty()) {
                throw std::runtime_error(
                    "redstoneControlledPowerInverted requires redstoneControlledProperty for block: " +
                    def.namespacedId.full());
            }
            def.redstoneControlledPowerInverted = blockJson["redstoneControlledPowerInverted"].get<bool>();
        }
        const bool hasExplicitMaterialKind = parseMaterialKind(blockJson, def.materialKind);
        const bool hasExplicitDerivativeMaterialId = parseDerivativeMaterialId(blockJson, def.derivativeMaterialId);

        bool hasTintedTexture = false;
        bool hasUntintedTexture = false;
        BiomeTintKind textureTint = BiomeTintKind::None;

        auto resolveTextureName = [&](const std::string& name) -> AnimatedTextureRef {
#ifdef MECRAFT_NO_TEXTURES
            static_cast<void>(name);
            return makeStaticWorldTexture(0);
#else
            if (resourceMgr == nullptr) {
                return makeStaticWorldTexture(0);
            }
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

        auto resolveTextureKey = [&](const nlohmann::json& tex,
                                     const char* key,
                                     const std::string& context) -> AnimatedTextureRef {
            if (!tex.contains(key)) {
                return makeStaticWorldTexture(0);
            }
            if (!tex[key].is_string()) {
                throw std::runtime_error("Block texture key must be a string: " + context + "." + key);
            }
            return resolveTextureName(tex[key].get<std::string>());
        };

        auto applyTextureObjectToBlock = [&](const nlohmann::json& tex,
                                             const std::string& context) {
            for (auto it = tex.begin(); it != tex.end(); ++it) {
                if (!it.value().is_string()) {
                    throw std::runtime_error("Block texture key must be a string: " + context + "." + it.key());
                }
                def.namedTextureRefs[it.key()] = resolveTextureName(it.value().get<std::string>());
            }
            if (tex.contains("all")) {
                setAllFaces(def, resolveTextureKey(tex, "all", context));
            }
            if (tex.contains("top")) {
                def.faceTop = resolveTextureKey(tex, "top", context);
            }
            if (tex.contains("bottom")) {
                def.faceBottom = resolveTextureKey(tex, "bottom", context);
            }
            if (tex.contains("side")) {
                const AnimatedTextureRef ref = resolveTextureKey(tex, "side", context);
                def.faceLeft = ref;
                def.faceRight = ref;
                def.faceFront = ref;
                def.faceBack = ref;
            }
            if (tex.contains("left")) {
                def.faceLeft = resolveTextureKey(tex, "left", context);
            }
            if (tex.contains("right")) {
                def.faceRight = resolveTextureKey(tex, "right", context);
            }
            if (tex.contains("front")) {
                def.faceFront = resolveTextureKey(tex, "front", context);
            }
            if (tex.contains("back")) {
                def.faceBack = resolveTextureKey(tex, "back", context);
            }
        };

        auto parseTextureFaces = [&](const nlohmann::json& tex,
                                     const BlockDef& baseDef,
                                     const std::string& context) -> BlockTextureFaces {
            if (!tex.is_object()) {
                throw std::runtime_error("State texture entry must be an object: " + context);
            }

            BlockTextureFaces faces = textureFacesFromBlock(baseDef);
            if (tex.contains("all")) {
                setAllFaces(faces, resolveTextureKey(tex, "all", context));
            }
            if (tex.contains("top")) {
                faces.faceTop = resolveTextureKey(tex, "top", context);
            }
            if (tex.contains("bottom")) {
                faces.faceBottom = resolveTextureKey(tex, "bottom", context);
            }
            if (tex.contains("side")) {
                const AnimatedTextureRef ref = resolveTextureKey(tex, "side", context);
                faces.faceLeft = ref;
                faces.faceRight = ref;
                faces.faceFront = ref;
                faces.faceBack = ref;
            }
            if (tex.contains("left")) {
                faces.faceLeft = resolveTextureKey(tex, "left", context);
            }
            if (tex.contains("right")) {
                faces.faceRight = resolveTextureKey(tex, "right", context);
            }
            if (tex.contains("front")) {
                faces.faceFront = resolveTextureKey(tex, "front", context);
            }
            if (tex.contains("back")) {
                faces.faceBack = resolveTextureKey(tex, "back", context);
            }
            return faces;
        };

        if (blockJson.contains("textures") && blockJson["textures"].is_object()) {
            const auto& tex = blockJson["textures"];
            applyTextureObjectToBlock(tex, def.namespacedId.full());
        }

        if (blockJson.contains("stateTextures")) {
            if (!blockJson["stateTextures"].is_object()) {
                throw std::runtime_error("stateTextures must be an object for block: " + def.namespacedId.full());
            }

            for (auto propertyIt = blockJson["stateTextures"].begin();
                 propertyIt != blockJson["stateTextures"].end();
                 ++propertyIt) {
                if (!propertyIt.value().is_object()) {
                    throw std::runtime_error("stateTextures property entry must be an object: " +
                                             def.namespacedId.full() + "." + propertyIt.key());
                }

                StateTextureRule rule;
                rule.propertyName = propertyIt.key();
                for (auto valueIt = propertyIt.value().begin(); valueIt != propertyIt.value().end(); ++valueIt) {
                    rule.texturesByValue.emplace(
                        valueIt.key(),
                        parseTextureFaces(valueIt.value(),
                                          def,
                                          def.namespacedId.full() + "." + propertyIt.key() + "=" + valueIt.key()));
                }
                def.stateTextureRules.push_back(std::move(rule));
            }
        }

        if (blockJson.contains("randomTick")) {
            const auto& randomTickJson = blockJson["randomTick"];
            if (!randomTickJson.is_object()) {
                throw std::runtime_error("randomTick must be an object for block: " + def.namespacedId.full());
            }
            const auto behaviorIt = randomTickJson.find("behavior");
            if (behaviorIt == randomTickJson.end() || !behaviorIt->is_string()) {
                throw std::runtime_error("randomTick.behavior must be a string for block: " + def.namespacedId.full());
            }

            def.randomTick.enabled = true;
            def.randomTick.behavior = behaviorIt->get<std::string>();

            const auto propertyIt = randomTickJson.find("property");
            if (propertyIt != randomTickJson.end()) {
                if (!propertyIt->is_string()) {
                    throw std::runtime_error("randomTick.property must be a string for block: " + def.namespacedId.full());
                }
                def.randomTick.propertyName = propertyIt->get<std::string>();
            }

            const auto chanceIt = randomTickJson.find("chance");
            if (chanceIt != randomTickJson.end()) {
                if (!chanceIt->is_number()) {
                    throw std::runtime_error("randomTick.chance must be numeric for block: " + def.namespacedId.full());
                }
                def.randomTick.chance = chanceIt->get<float>();
                if (def.randomTick.chance < 0.0f || def.randomTick.chance > 1.0f) {
                    throw std::runtime_error("randomTick.chance must be between 0 and 1 for block: " + def.namespacedId.full());
                }
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

        bool redstoneControlledPropertyValidated = def.redstoneControlledProperty.empty();
        bool redstoneControlledMirrorPropertiesValidated = def.redstoneControlledMirrorProperties.empty();
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

            if (!def.redstoneControlledProperty.empty()) {
                const auto propertyIt = std::find_if(
                    properties.begin(),
                    properties.end(),
                    [&](const auto& property) {
                        return property.first == def.redstoneControlledProperty;
                    });
                if (propertyIt == properties.end()) {
                    throw std::runtime_error("redstoneControlledProperty must be declared in properties for block: " +
                                             def.namespacedId.full());
                }
                const auto& values = propertyIt->second;
                if (std::find(values.begin(), values.end(), "false") == values.end() ||
                    std::find(values.begin(), values.end(), "true") == values.end()) {
                    throw std::runtime_error(
                        "redstoneControlledProperty must declare false and true values for block: " +
                        def.namespacedId.full());
                }
                redstoneControlledPropertyValidated = true;
            }
            if (!def.redstoneControlledMirrorProperties.empty()) {
                for (const std::string& mirrorProperty : def.redstoneControlledMirrorProperties) {
                    const auto propertyIt = std::find_if(
                        properties.begin(),
                        properties.end(),
                        [&](const auto& property) {
                            return property.first == mirrorProperty;
                        });
                    if (propertyIt == properties.end()) {
                        throw std::runtime_error("redstoneControlledMirrorProperties entry must be declared in properties for block: " +
                                                 def.namespacedId.full() + "." + mirrorProperty);
                    }
                    const auto& values = propertyIt->second;
                    if (std::find(values.begin(), values.end(), "false") == values.end() ||
                        std::find(values.begin(), values.end(), "true") == values.end()) {
                        throw std::runtime_error(
                            "redstoneControlledMirrorProperties entries must declare false and true values for block: " +
                            def.namespacedId.full() + "." + mirrorProperty);
                    }
                }
                redstoneControlledMirrorPropertiesValidated = true;
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
        if (!redstoneControlledPropertyValidated) {
            throw std::runtime_error("redstoneControlledProperty requires a properties object for block: " +
                                     def.namespacedId.full());
        }
        if (!redstoneControlledMirrorPropertiesValidated) {
            throw std::runtime_error("redstoneControlledMirrorProperties requires a properties object for block: " +
                                     def.namespacedId.full());
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
        throw std::runtime_error("Block is not registered: " + name);
    }
    return outId;
}

BlockID BlockRegistry::requireIdByName(const std::string& name) {
    BlockID outId = 0;
    if (!tryGetIdByName(name, outId)) {
        throw std::runtime_error("Required block is not registered: " + name);
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
    throw std::runtime_error("Block is not registered: " + namespacedId.full());
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

bool BlockRegistry::hasTag(const BlockID id, const NamespacedId& tag) {
    const BlockDef& def = getFast(id);
    return std::find(def.tags.begin(), def.tags.end(), tag) != def.tags.end();
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
