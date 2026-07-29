//
// Created by Caiwe on 2026/3/24.
//

#ifndef MECRAFT_BLOCK_H
#define MECRAFT_BLOCK_H
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <array>
#include <limits>
#include <optional>
#include <string_view>
#include <unordered_map>

#include <glm/vec3.hpp>

#include "../../MecraftExport.h"
#include "../../engine/registry/NamespacedId.h"
#include "../../engine/registry/IdRegistry.h"

class ResourceMgr;

// BlockID identifies block definitions; concrete placed variants use BlockStateId.
using BlockID = RuntimeId;

namespace BlockMaterialKinds {
// Legacy broad categories used by CPU-side gameplay helpers. The renderer
// consumes derivativeMaterialId below for shaderpack-compatible material logic.
constexpr uint8_t DEFAULT = 0;
constexpr uint8_t STONE = 1;
constexpr uint8_t DIRT = 2;
constexpr uint8_t GRASS = 3;
constexpr uint8_t WOOD = 4;
constexpr uint8_t LEAVES = 5;
constexpr uint8_t PLANT = 6;
constexpr uint8_t SAND = 7;
constexpr uint8_t GLASS = 8;
constexpr uint8_t WATER = 9;
constexpr uint8_t ORE = 10;
constexpr uint8_t EMISSIVE = 11;
constexpr uint8_t METAL = 12;
constexpr uint8_t ICE = 13;
constexpr uint8_t STAINED_GLASS = 14;
constexpr uint8_t MAX_BUILTIN = STAINED_GLASS;
}

namespace DerivativeMaterialIds {
// Mirrored by assets/shaders/gbuffer_contract.glsl and DerivativeMain/block.properties.
constexpr uint8_t DEFAULT = 0;
constexpr uint8_t GRASS = 1;
constexpr uint8_t WHEAT = 2;
constexpr uint8_t FLOWER = 3;
constexpr uint8_t GRASS_UPPER = 4;
constexpr uint8_t GRASS_LOWER = 5;
constexpr uint8_t GRASS_LIKE = 6;
constexpr uint8_t LEAVES = 7;
constexpr uint8_t BANNER_SSS = 9;
constexpr uint8_t SNOW_ICE_SSS = 10;
constexpr uint8_t LAVA = 15;
constexpr uint8_t STAINED_GLASS = 16;
constexpr uint8_t WATER = 17;
constexpr uint8_t ICE = 18;
constexpr uint8_t END_PORTAL = 19;
constexpr uint8_t TOTAL_GLOWING = 20;
constexpr uint8_t TORCH_LIKE = 21;
constexpr uint8_t FIRE = 22;
constexpr uint8_t GLOWSTONE_LIKE = 23;
constexpr uint8_t SEA_LANTERN_LIKE = 24;
constexpr uint8_t REDSTONE = 25;
constexpr uint8_t SOUL_FIRE = 26;
constexpr uint8_t AMETHYST = 27;
constexpr uint8_t GLOWBERRY = 28;
constexpr uint8_t RAILS = 29;
constexpr uint8_t BEACON_CORE = 30;
constexpr uint8_t SCULK = 31;
constexpr uint8_t GLOW_LICHEN = 32;
constexpr uint8_t PARTIAL_GLOWING = 33;
constexpr uint8_t MIDDLE_GLOWING = 34;
constexpr uint8_t TEXTURED = 35;
constexpr uint8_t TEXTURED_EMISSIVE = 36;
constexpr uint8_t ORE = 57;
constexpr uint8_t NETHER_ORE = 58;
constexpr uint8_t MAX_BUILTIN = NETHER_ORE;
}

namespace BlockMaterialFlags {
constexpr uint16_t None = 0;
constexpr uint16_t Solid = 1u << 0u;
constexpr uint16_t Vegetation = 1u << 1u;
constexpr uint16_t Translucent = 1u << 2u;
constexpr uint16_t Water = 1u << 3u;
constexpr uint16_t Emissive = 1u << 4u;
constexpr uint16_t Reflective = 1u << 5u;
constexpr uint16_t Metallic = 1u << 6u;
constexpr uint16_t Subsurface = 1u << 7u;
constexpr uint16_t Terrain = 1u << 8u;
}

struct BlockMaterialInfo {
    uint8_t kind = BlockMaterialKinds::DEFAULT;
    const char* name = "default";
    uint16_t flags = BlockMaterialFlags::None;
    float roughness = 0.84f;
    float f0 = 0.040f;
    float emission = 0.0f;
    float subsurface = 0.0f;
};

namespace BlockMaterials {
inline constexpr std::array<BlockMaterialInfo, BlockMaterialKinds::MAX_BUILTIN + 1> BUILTIN = {{
    {BlockMaterialKinds::DEFAULT, "default", BlockMaterialFlags::Terrain, 1.0f, 0.0f, 0.0f, 0.0f},
    {BlockMaterialKinds::STONE, "stone", BlockMaterialFlags::Solid | BlockMaterialFlags::Terrain, 1.0f, 0.0f, 0.0f, 0.0f},
    {BlockMaterialKinds::DIRT, "dirt", BlockMaterialFlags::Solid | BlockMaterialFlags::Terrain, 1.0f, 0.0f, 0.0f, 0.0f},
    {BlockMaterialKinds::GRASS, "grass", BlockMaterialFlags::Solid | BlockMaterialFlags::Terrain | BlockMaterialFlags::Vegetation, 1.0f, 0.0f, 0.0f, 0.0f},
    {BlockMaterialKinds::WOOD, "wood", BlockMaterialFlags::Solid | BlockMaterialFlags::Terrain, 1.0f, 0.0f, 0.0f, 0.0f},
    {BlockMaterialKinds::LEAVES, "leaves", BlockMaterialFlags::Vegetation | BlockMaterialFlags::Subsurface, 1.0f, 0.0f, 0.0f, 0.70f},
    {BlockMaterialKinds::PLANT, "plant", BlockMaterialFlags::Vegetation | BlockMaterialFlags::Subsurface, 1.0f, 0.0f, 0.0f, 0.45f},
    {BlockMaterialKinds::SAND, "sand", BlockMaterialFlags::Solid | BlockMaterialFlags::Terrain, 1.0f, 0.0f, 0.0f, 0.0f},
    {BlockMaterialKinds::GLASS, "glass", BlockMaterialFlags::Translucent | BlockMaterialFlags::Reflective, 0.08f, 0.060f, 0.0f, 0.0f},
    {BlockMaterialKinds::WATER, "water", BlockMaterialFlags::Translucent | BlockMaterialFlags::Water | BlockMaterialFlags::Reflective, 0.03f, 0.020f, 0.0f, 0.0f},
    {BlockMaterialKinds::ORE, "ore", BlockMaterialFlags::Solid | BlockMaterialFlags::Terrain, 1.0f, 0.0f, 0.0f, 0.0f},
    {BlockMaterialKinds::EMISSIVE, "emissive", BlockMaterialFlags::Solid | BlockMaterialFlags::Emissive, 0.44f, 0.060f, 1.0f, 0.0f},
    {BlockMaterialKinds::METAL, "metal", BlockMaterialFlags::Solid | BlockMaterialFlags::Reflective | BlockMaterialFlags::Metallic, 0.30f, 0.260f, 0.0f, 0.0f},
}};

static_assert(BUILTIN.size() == BlockMaterialKinds::MAX_BUILTIN + 1, "Block material table must cover all builtin material ids.");
static_assert(BUILTIN[BlockMaterialKinds::DEFAULT].kind == BlockMaterialKinds::DEFAULT, "Block material table order mismatch.");
static_assert(BUILTIN[BlockMaterialKinds::STONE].kind == BlockMaterialKinds::STONE, "Block material table order mismatch.");
static_assert(BUILTIN[BlockMaterialKinds::DIRT].kind == BlockMaterialKinds::DIRT, "Block material table order mismatch.");
static_assert(BUILTIN[BlockMaterialKinds::GRASS].kind == BlockMaterialKinds::GRASS, "Block material table order mismatch.");
static_assert(BUILTIN[BlockMaterialKinds::WOOD].kind == BlockMaterialKinds::WOOD, "Block material table order mismatch.");
static_assert(BUILTIN[BlockMaterialKinds::LEAVES].kind == BlockMaterialKinds::LEAVES, "Block material table order mismatch.");
static_assert(BUILTIN[BlockMaterialKinds::PLANT].kind == BlockMaterialKinds::PLANT, "Block material table order mismatch.");
static_assert(BUILTIN[BlockMaterialKinds::SAND].kind == BlockMaterialKinds::SAND, "Block material table order mismatch.");
static_assert(BUILTIN[BlockMaterialKinds::GLASS].kind == BlockMaterialKinds::GLASS, "Block material table order mismatch.");
static_assert(BUILTIN[BlockMaterialKinds::WATER].kind == BlockMaterialKinds::WATER, "Block material table order mismatch.");
static_assert(BUILTIN[BlockMaterialKinds::ORE].kind == BlockMaterialKinds::ORE, "Block material table order mismatch.");
static_assert(BUILTIN[BlockMaterialKinds::EMISSIVE].kind == BlockMaterialKinds::EMISSIVE, "Block material table order mismatch.");
static_assert(BUILTIN[BlockMaterialKinds::METAL].kind == BlockMaterialKinds::METAL, "Block material table order mismatch.");

[[nodiscard]] inline constexpr const BlockMaterialInfo& get(const uint8_t kind) {
    return kind <= BlockMaterialKinds::MAX_BUILTIN ? BUILTIN[kind] : BUILTIN[BlockMaterialKinds::DEFAULT];
}

[[nodiscard]] inline constexpr bool hasFlag(const uint8_t kind, const uint16_t flag) {
    return (get(kind).flags & flag) != 0;
}

[[nodiscard]] inline constexpr bool isWater(const uint8_t kind) {
    return hasFlag(kind, BlockMaterialFlags::Water);
}

[[nodiscard]] inline constexpr bool isTranslucent(const uint8_t kind) {
    return hasFlag(kind, BlockMaterialFlags::Translucent);
}

[[nodiscard]] inline constexpr bool isVegetation(const uint8_t kind) {
    return hasFlag(kind, BlockMaterialFlags::Vegetation);
}

[[nodiscard]] inline constexpr bool isEmissive(const uint8_t kind) {
    return hasFlag(kind, BlockMaterialFlags::Emissive);
}

[[nodiscard]] inline constexpr bool isReflective(const uint8_t kind) {
    return hasFlag(kind, BlockMaterialFlags::Reflective);
}

[[nodiscard]] inline bool tryParseKindName(const std::string_view name, uint8_t& outKind) {
    for (const BlockMaterialInfo& material : BUILTIN) {
        if (name == material.name) {
            outKind = material.kind;
            return true;
        }
    }
    return false;
}
}

enum class BlockRenderShape : uint8_t {
    Cube = 0,
    Cross = 1,
    Custom = 2
};

enum class BiomeTintKind : uint8_t {
    None = 0,
    Grass = 1,
    Foliage = 2,
};

enum class BlockRenderLayer : uint8_t {
    Opaque = 0,
    Cutout = 1,
    Transparent = 2,
};

struct AnimatedTextureRef {
    int firstLayer = 0;
    uint16_t frameCount = 1;
    float fps = 0.0f;
    bool isAnimated = false;
    BiomeTintKind tint = BiomeTintKind::None;
};

struct NamedTextureAnimation {
    std::string textureName;
    AnimatedTextureRef ref;
};

struct BlockTextureFaces {
    AnimatedTextureRef faceTop;
    AnimatedTextureRef faceBottom;
    AnimatedTextureRef faceLeft;
    AnimatedTextureRef faceRight;
    AnimatedTextureRef faceFront;
    AnimatedTextureRef faceBack;
};

struct StateTextureRule {
    std::string propertyName;
    std::unordered_map<std::string, BlockTextureFaces> texturesByValue;
};

struct BlockRandomTickRule {
    bool enabled = false;
    std::string behavior;
    std::string propertyName;
    float chance = 1.0f;
};

/// Defines the physical point-light proxy emitted by one placed block.
/// Block light propagation remains a separate gameplay data product.
struct BlockAnalyticLightDefinition {
    static constexpr uint16_t kUnconditionalStateIndex =
        std::numeric_limits<uint16_t>::max();

    /// Linear RGB light chromaticity multiplied by the normalized intensity.
    glm::vec3 colorLinear{1.0f};
    /// Light position within the block-local unit cube, measured in meters.
    glm::vec3 positionOffsetMeters{0.5f};
    /// Authoring luminous flux converted to candela by the GPU light contract.
    float luminousFluxLumens = 0.0f;
    /// Finite influence radius used by clustered-light bounds and attenuation.
    float rangeMeters = 0.0f;
    /// Optional block-state property and value required to emit this light.
    uint16_t enabledStatePropertyIndex = kUnconditionalStateIndex;
    uint16_t enabledStateValueIndex = kUnconditionalStateIndex;
};

struct BlockDef {
    NamespacedId namespacedId = NamespacedId("minecraft", "unknown");
    bool isSolid        = true;
    bool isTransparent  = false;
    bool isLightSource  = false;
    bool isSelectable   = true;
    bool allowsFluidCoexistence = false;
    bool affectedByGravity = false;   // gravity-affected blocks fall as entities when unsupported (sand/gravel)
    BlockRenderShape renderShape = BlockRenderShape::Cube;
    BlockRenderLayer renderLayer = BlockRenderLayer::Opaque;
    bool cutoutDistanceCull = true;
    std::string renderShapeName = "cube";
    uint8_t renderShapeTag = 0;
    uint8_t materialKind = 0;
    uint8_t derivativeMaterialId = DerivativeMaterialIds::DEFAULT;
    bool faceOrientedModel = false;
    std::string placementStrategy = "simple";
    bool revertPlacementFacing = false;
    std::string supportRule;
    std::string containerUi;
    std::string interaction;
    std::vector<NamespacedId> tags;
    BiomeTintKind biomeTint = BiomeTintKind::None;
    uint8_t lightLevel  = 0;
    uint8_t opacity     = 0;
    std::optional<BlockAnalyticLightDefinition> analyticLight;
    uint16_t timeToBreak = 1000;
    float surfaceFriction = 1.0f;
    float surfaceSpeedFactor = 1.0f;
    float surfaceDamping = 0.0f;
    // Unified face texture references (TextureArray layer indices).
    // These are the single source of truth for all texture lookups.
    AnimatedTextureRef faceTop;
    AnimatedTextureRef faceBottom;
    AnimatedTextureRef faceLeft;
    AnimatedTextureRef faceRight;
    AnimatedTextureRef faceFront;
    AnimatedTextureRef faceBack;
    std::unordered_map<std::string, AnimatedTextureRef> namedTextureRefs;
    std::unordered_map<std::string, NamedTextureAnimation> namedTextureAnimations;
    std::vector<StateTextureRule> stateTextureRules;
    BlockRandomTickRule randomTick;
    bool isRedstoneConductor = false;      // True when this block can transfer redstone power through its body.
    bool isRedstonePowerSource = false;    // True when this block can emit redstone power.
    bool respondsToRedstone = false;       // True when this block changes state after receiving redstone power.
    uint8_t redstonePowerOutput = 0;       // Fixed output strength in the inclusive range [0, 15].
    uint64_t redstonePulseTicks = 0;       // Scheduled pulse duration for momentary redstone devices.
    std::string redstoneBehavior;          // Behavior tag used by redstone systems and device-specific logic.
    std::string redstoneWireChannel;       // Wire network key; wires only connect and propagate to matching channels.
    uint16_t redstoneWireChannelId = 0;    // Parsed wire network id used by hot-path wire comparisons.
    uint8_t redstoneWireTint = 0;          // Redstone shader tint palette index in the inclusive range [0, 15].
    bool isWireContainer = false;          // True when the block hosts multiple redstone wire parts.
    std::string pressurePlateEntityFilter; // Entity filter used by pressure plate contact evaluation.
    std::string redstoneControlledProperty; // Boolean state property driven by incoming redstone power.
    std::vector<std::string> redstoneControlledMirrorProperties; // Boolean state properties updated when the controlled property changes.
    bool redstoneControlledPowerInverted = false; // True when powered blocks select the false property value.
    std::string pistonPushReaction = "normal"; // Controls how pistons treat this block during movement.

    // Convenience: return the TextureArray first layer for a given face (0=top,1=bottom,2=front,3=back,4=left,5=right)
    [[nodiscard]] int getFaceLayer(int face) const {
        switch (face) {
            case 0: return faceTop.firstLayer;
            case 1: return faceBottom.firstLayer;
            case 2: return faceFront.firstLayer;
            case 3: return faceBack.firstLayer;
            case 4: return faceLeft.firstLayer;
            case 5: return faceRight.firstLayer;
            default: return faceTop.firstLayer;
        }
    }
};

[[nodiscard]] inline bool isWaterMaterial(const BlockDef& def) {
    return BlockMaterials::isWater(def.materialKind);
}

[[nodiscard]] inline bool usesWaterRendering(const BlockDef& def) {
    return def.renderLayer == BlockRenderLayer::Transparent && isWaterMaterial(def);
}

class BlockRegistry {
public:
    static void init(ResourceMgr* resourceMgr = nullptr);
    static void ensureInitialized();
    static const BlockDef& get(BlockID id);
    static const BlockDef& getFast(BlockID id);
    [[nodiscard]] static uint8_t getOpacityFast(BlockID id);
    [[nodiscard]] static uint8_t getLightLevelFast(BlockID id);
    [[nodiscard]] static bool isLightSourceFast(BlockID id);
    [[nodiscard]] static BlockID findByName(const std::string& name);
    [[nodiscard]] static BlockID requireIdByName(const std::string& name);
    [[nodiscard]] static bool tryGetIdByName(const std::string& name, BlockID& outId);
    [[nodiscard]] static BlockID getId(const NamespacedId& namespacedId);
    [[nodiscard]] static bool tryGetId(const NamespacedId& namespacedId, BlockID& outId);
    [[nodiscard]] static const NamespacedId& getNamespacedId(BlockID runtimeId);
    [[nodiscard]] static const NamespacedId& getBlockDropId(BlockID id);
    [[nodiscard]] static bool hasTag(BlockID id, const NamespacedId& tag);
    static void printAllBlocks();

    // Register a new block (Mod API)
    static BlockID registerBlock(const NamespacedId& id, BlockDef def);

    // Get the number of registered blocks
    [[nodiscard]] static size_t getBlockCount();

private:
    static IdRegistry s_idRegistry;
    static std::vector<BlockDef> s_blocks;                        // index = BlockID (RuntimeId)
    static std::vector<NamespacedId> s_blockDropIds;               // index = BlockID
    static std::unordered_map<NamespacedId, BlockID> s_idLookup;  // fast reverse lookup
    static bool s_initialized;
};


#endif //MECRAFT_BLOCK_H
