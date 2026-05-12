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
#include <string_view>
#include <unordered_map>

#include "../core/NamespacedId.h"
#include "../core/BuiltinIds.h"
#include "../core/IdRegistry.h"

class ResourceMgr;

// BlockID is now RuntimeId (uint16_t), allowing up to 65535 block types
using BlockID = RuntimeId;

// Block ID constants — initialized after BlockRegistry::init()
namespace BlockIds {
#define MECRAFT_DECLARE_BLOCK_ID(symbol, path) extern BlockID symbol;
    MECRAFT_FOR_EACH_BUILTIN_BLOCK(MECRAFT_DECLARE_BLOCK_ID)
#undef MECRAFT_DECLARE_BLOCK_ID

    void init();  // Called after BlockRegistry::init()
}

namespace BlockMaterialKinds {
// Mirrored by assets/shaders/gbuffer_contract.glsl.
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
    {BlockMaterialKinds::DEFAULT, "default", BlockMaterialFlags::Terrain, 0.84f, 0.040f, 0.0f, 0.0f},
    {BlockMaterialKinds::STONE, "stone", BlockMaterialFlags::Solid | BlockMaterialFlags::Terrain, 0.78f, 0.055f, 0.0f, 0.0f},
    {BlockMaterialKinds::DIRT, "dirt", BlockMaterialFlags::Solid | BlockMaterialFlags::Terrain, 0.96f, 0.030f, 0.0f, 0.0f},
    {BlockMaterialKinds::GRASS, "grass", BlockMaterialFlags::Solid | BlockMaterialFlags::Terrain | BlockMaterialFlags::Vegetation | BlockMaterialFlags::Subsurface, 0.88f, 0.035f, 0.0f, 0.26f},
    {BlockMaterialKinds::WOOD, "wood", BlockMaterialFlags::Solid | BlockMaterialFlags::Terrain, 0.68f, 0.050f, 0.0f, 0.0f},
    {BlockMaterialKinds::LEAVES, "leaves", BlockMaterialFlags::Vegetation | BlockMaterialFlags::Subsurface, 0.74f, 0.040f, 0.0f, 0.72f},
    {BlockMaterialKinds::PLANT, "plant", BlockMaterialFlags::Vegetation | BlockMaterialFlags::Subsurface, 0.82f, 0.032f, 0.0f, 0.78f},
    {BlockMaterialKinds::SAND, "sand", BlockMaterialFlags::Solid | BlockMaterialFlags::Terrain, 0.92f, 0.026f, 0.0f, 0.0f},
    {BlockMaterialKinds::GLASS, "glass", BlockMaterialFlags::Translucent | BlockMaterialFlags::Reflective, 0.08f, 0.060f, 0.0f, 0.0f},
    {BlockMaterialKinds::WATER, "water", BlockMaterialFlags::Translucent | BlockMaterialFlags::Water | BlockMaterialFlags::Reflective, 0.03f, 0.020f, 0.0f, 0.0f},
    {BlockMaterialKinds::ORE, "ore", BlockMaterialFlags::Solid | BlockMaterialFlags::Terrain | BlockMaterialFlags::Reflective, 0.42f, 0.120f, 0.0f, 0.0f},
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
};

struct NamedTextureAnimation {
    std::string textureName;
    AnimatedTextureRef ref;
};

struct BlockDef {
    NamespacedId namespacedId = NamespacedId("minecraft", "unknown");
    bool isSolid        = true;
    bool isTransparent  = false;
    bool isLightSource  = false;
    bool isSelectable   = true;
    bool allowsFluidCoexistence = false;
    BlockRenderShape renderShape = BlockRenderShape::Cube;
    BlockRenderLayer renderLayer = BlockRenderLayer::Opaque;
    bool cutoutDistanceCull = true;
    std::string renderShapeName = "cube";
    uint8_t renderShapeTag = 0;
    uint8_t materialKind = 0;
    std::string placementStrategy = "simple";
    std::string supportRule;
    BiomeTintKind biomeTint = BiomeTintKind::None;
    uint8_t lightLevel  = 0;
    uint8_t opacity     = 0;
    uint16_t timeToBreak = 1000;
    // Six face texture atlas tile indices
    int texTop = 0;
    int texBottom = 0;
    int texLeft = 0;
    int texRight = 0;
    int texFront = 0;
    int texBack = 0;
    AnimatedTextureRef worldTop;
    AnimatedTextureRef worldBottom;
    AnimatedTextureRef worldLeft;
    AnimatedTextureRef worldRight;
    AnimatedTextureRef worldFront;
    AnimatedTextureRef worldBack;
    std::unordered_map<std::string, NamedTextureAnimation> namedTextureAnimations;
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
    [[nodiscard]] static bool tryGetIdByName(const std::string& name, BlockID& outId);
    [[nodiscard]] static BlockID getId(const NamespacedId& namespacedId);
    [[nodiscard]] static bool tryGetId(const NamespacedId& namespacedId, BlockID& outId);
    [[nodiscard]] static const NamespacedId& getNamespacedId(BlockID runtimeId);
    [[nodiscard]] static const NamespacedId& getBlockDropId(BlockID id);
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
