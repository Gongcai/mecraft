//
// Created by Caiwe on 2026/3/24.
//

#ifndef MECRAFT_BLOCK_H
#define MECRAFT_BLOCK_H
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
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
