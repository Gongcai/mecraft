#include "app/validation/ValidationVoxelFixture.h"

#include "renderer/contracts/ContentHashContract.h"
#include "world/IWorldView.h"
#include "world/World.h"
#include "world/block/Block.h"
#include "world/block/BlockStateRegistry.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <cstdlib>
#include <string_view>
#include <vector>

namespace app::validation {
namespace {

constexpr std::string_view kAirBlockId = "minecraft:air";
constexpr std::string_view kStoneBlockId = "minecraft:stone";
constexpr std::string_view kOakPlanksBlockId = "minecraft:oak_planks";
constexpr std::string_view kSprucePlanksBlockId = "minecraft:spruce_planks";
constexpr std::string_view kGlassBlockId = "minecraft:glass";
constexpr std::string_view kTorchBlockId = "minecraft:torch";
constexpr std::string_view kGlowstoneBlockId = "minecraft:glowstone";
constexpr std::string_view kMagmaBlockId = "minecraft:magma_block";
constexpr std::string_view kIronBlockId = "minecraft:iron_block";
constexpr std::string_view kGoldBlockId = "minecraft:gold_block";
constexpr std::string_view kDiamondBlockId = "minecraft:diamond_block";
constexpr std::string_view kGrassBlockId = "minecraft:grass_block";
constexpr std::string_view kOakLogBlockId = "minecraft:oak_log";
constexpr std::string_view kOakLeavesBlockId = "minecraft:oak_leaves";
constexpr std::string_view kTallGrassBlockId = "minecraft:tall_grass";

struct FixtureEdit final {
    glm::ivec3 position{0};
    std::string_view blockId;
};

enum class FixtureKind : uint8_t { None, WindowRoom, CaveTurn, LocalLightVillage, ForestCutout };

struct FixtureBlockStates final {
    BlockStateId air = NULL_BLOCK_STATE;
    BlockStateId stone = NULL_BLOCK_STATE;
    BlockStateId oakPlanks = NULL_BLOCK_STATE;
    BlockStateId sprucePlanks = NULL_BLOCK_STATE;
    BlockStateId glass = NULL_BLOCK_STATE;
    BlockStateId torch = NULL_BLOCK_STATE;
    BlockStateId glowstone = NULL_BLOCK_STATE;
    BlockStateId magma = NULL_BLOCK_STATE;
    BlockStateId iron = NULL_BLOCK_STATE;
    BlockStateId gold = NULL_BLOCK_STATE;
    BlockStateId diamond = NULL_BLOCK_STATE;
    BlockStateId grass = NULL_BLOCK_STATE;
    BlockStateId oakLog = NULL_BLOCK_STATE;
    BlockStateId oakLeaves = NULL_BLOCK_STATE;
    BlockStateId tallGrass = NULL_BLOCK_STATE;
};

[[nodiscard]] bool fixtureKind(const ValidationVoxelFixtureIdentity& fixture, FixtureKind& kind) {
    if (fixture.version != kValidationVoxelFixtureVersion) {
        return false;
    }
    if (fixture.id == kValidationVoxelFixtureNoneId) {
        kind = FixtureKind::None;
        return true;
    }
    if (fixture.id == kValidationVoxelFixtureWindowRoomId) {
        kind = FixtureKind::WindowRoom;
        return true;
    }
    if (fixture.id == kValidationVoxelFixtureCaveTurnId) {
        kind = FixtureKind::CaveTurn;
        return true;
    }
    if (fixture.id == kValidationVoxelFixtureLocalLightVillageId) {
        kind = FixtureKind::LocalLightVillage;
        return true;
    }
    if (fixture.id == kValidationVoxelFixtureForestCutoutId) {
        kind = FixtureKind::ForestCutout;
        return true;
    }
    return false;
}

[[nodiscard]] std::string_view windowRoomBlock(const int x, const int y, const int z) {
    if (y == 78 || y == 87) {
        return kStoneBlockId;
    }
    const bool wall = x == -8 || x == 8 || z == -8 || z == 8;
    if (wall && y >= 79 && y <= 86) {
        const bool window = z == 8 && x >= -3 && x <= 3 && y >= 81 && y <= 84;
        return window ? kGlassBlockId : kStoneBlockId;
    }
    if (z == 2 && y >= 79 && y <= 81) {
        if (x == -4) {
            return kIronBlockId;
        }
        if (x == 0) {
            return kGoldBlockId;
        }
        if (x == 4) {
            return kDiamondBlockId;
        }
    }
    if (y == 80 && z == -5) {
        if (x == -2) {
            return kGlowstoneBlockId;
        }
        if (x == 2) {
            return kMagmaBlockId;
        }
    }
    return kAirBlockId;
}

[[nodiscard]] std::string_view caveTurnBlock(const int x, const int y, const int z) {
    const bool insideFirstLeg = x >= -2 && x <= 2 && z >= -2 && z <= 10;
    const bool insideSecondLeg = x >= -12 && x <= 2 && z >= -2 && z <= 2;
    const bool insideTunnel = y >= 80 && y <= 84 && (insideFirstLeg || insideSecondLeg);
    if (insideTunnel) {
        if (x == -8 && y == 84 && z == 0) {
            return kGlowstoneBlockId;
        }
        if (x == -11 && y >= 81 && y <= 82 && z == -2) {
            return kIronBlockId;
        }
        return kAirBlockId;
    }
    return kStoneBlockId;
}

[[nodiscard]] bool insideHouse(const int x, const int z, const int minX, const int maxX, const int minZ,
                               const int maxZ) {
    return x >= minX && x <= maxX && z >= minZ && z <= maxZ;
}

[[nodiscard]] std::string_view houseBlock(const int x, const int y, const int z, const int minX, const int maxX,
                                          const int minZ, const int maxZ, const int doorwayX, const int doorwayMinZ,
                                          const int doorwayMaxZ) {
    if (!insideHouse(x, z, minX, maxX, minZ, maxZ)) {
        return {};
    }
    if (y == 85) {
        return kSprucePlanksBlockId;
    }
    if (y < 79 || y > 84) {
        return {};
    }
    const bool boundary = x == minX || x == maxX || z == minZ || z == maxZ;
    if (!boundary) {
        return {};
    }
    if (x == doorwayX && z >= doorwayMinZ && z <= doorwayMaxZ && y <= 81) {
        return kAirBlockId;
    }
    const bool window = (z == minZ || z == maxZ) && x >= minX + 3 && x <= maxX - 3 && y >= 81 && y <= 82;
    return window ? kGlassBlockId : kOakPlanksBlockId;
}

[[nodiscard]] std::string_view localLightVillageBlock(const int x, const int y, const int z) {
    if (y == 78) {
        return kStoneBlockId;
    }

    const std::string_view leftHouse = houseBlock(x, y, z, -13, -3, -8, 0, -3, -5, -3);
    if (!leftHouse.empty()) {
        return leftHouse;
    }
    const std::string_view rightHouse = houseBlock(x, y, z, 3, 13, 0, 8, 3, 3, 5);
    if (!rightHouse.empty()) {
        return rightHouse;
    }

    if (y >= 79 && y <= 82 && x == -6 && z == -4) {
        return kIronBlockId;
    }
    if (y >= 79 && y <= 82 && x == 6 && z == 4) {
        return kGoldBlockId;
    }
    if (y == 79 && ((x == -9 && z == -4) || (x == 0 && z == 0) || (x == 9 && z == 4))) {
        return kTorchBlockId;
    }
    if (y == 84 && ((x == -8 && z == -4) || (x == 8 && z == 4))) {
        return kGlowstoneBlockId;
    }
    if (y == 79 && ((x == -1 && z >= -2 && z <= 2) || (x == 1 && z >= -2 && z <= 2))) {
        return kSprucePlanksBlockId;
    }
    return kAirBlockId;
}

[[nodiscard]] std::string_view forestCutoutBlock(const int x, const int y, const int z) {
    constexpr std::array<glm::ivec2, 12u> kTreeBases{{
        {-15, -14},
        {-7, -10},
        {1, -12},
        {10, -10},
        {-13, -2},
        {-4, -3},
        {5, -1},
        {14, 0},
        {-10, 7},
        {0, 6},
        {9, 8},
        {-2, 12},
    }};
    if (y == 78) {
        return kGrassBlockId;
    }
    for (const glm::ivec2 treeBase : kTreeBases) {
        const int deltaX = std::abs(x - treeBase.x);
        const int deltaZ = std::abs(z - treeBase.y);
        if (deltaX == 0 && deltaZ == 0 && y >= 79 && y <= 84) {
            return kOakLogBlockId;
        }
        const int canopyRadius = y == 82 || y == 87 ? 2 : (y >= 83 && y <= 86 ? 3 : 0);
        if (canopyRadius != 0 && deltaX <= canopyRadius && deltaZ <= canopyRadius &&
            (deltaX + deltaZ <= canopyRadius + 1 || (deltaX != canopyRadius && deltaZ != canopyRadius))) {
            return kOakLeavesBlockId;
        }
    }
    if (y == 79 && ((x * 17 + z * 31) & 3) == 0 && ((x + z) & 1) == 0) {
        return kTallGrassBlockId;
    }
    return kAirBlockId;
}

[[nodiscard]] std::vector<FixtureEdit> buildFixtureEdits(const FixtureKind kind) {
    std::vector<FixtureEdit> edits;
    switch (kind) {
    case FixtureKind::None: return edits;
    case FixtureKind::WindowRoom:
        edits.reserve(17u * 10u * 17u);
        for (int y = 78; y <= 87; ++y) {
            for (int z = -8; z <= 8; ++z) {
                for (int x = -8; x <= 8; ++x) {
                    edits.push_back({glm::ivec3(x, y, z), windowRoomBlock(x, y, z)});
                }
            }
        }
        return edits;
    case FixtureKind::CaveTurn:
        edits.reserve(21u * 9u * 19u);
        for (int y = 78; y <= 86; ++y) {
            for (int z = -6; z <= 12; ++z) {
                for (int x = -14; x <= 6; ++x) {
                    edits.push_back({glm::ivec3(x, y, z), caveTurnBlock(x, y, z)});
                }
            }
        }
        return edits;
    case FixtureKind::LocalLightVillage:
        edits.reserve(33u * 9u * 25u);
        for (int y = 78; y <= 86; ++y) {
            for (int z = -12; z <= 12; ++z) {
                for (int x = -16; x <= 16; ++x) {
                    edits.push_back({glm::ivec3(x, y, z), localLightVillageBlock(x, y, z)});
                }
            }
        }
        return edits;
    case FixtureKind::ForestCutout:
        edits.reserve(49u * 11u * 49u);
        for (int y = 78; y <= 88; ++y) {
            for (int z = -24; z <= 24; ++z) {
                for (int x = -24; x <= 24; ++x) {
                    edits.push_back({glm::ivec3(x, y, z), forestCutoutBlock(x, y, z)});
                }
            }
        }
        return edits;
    }
    std::abort();
}

[[nodiscard]] bool resolveBlockState(const std::string_view name, BlockStateId& state) {
    BlockID blockId = RUNTIME_ID_NULL;
    if (!BlockRegistry::tryGetIdByName(std::string(name), blockId)) {
        return false;
    }
    state = BlockStateRegistry::getDefaultState(blockId);
    return true;
}

[[nodiscard]] ValidationVoxelFixtureResult resolveFixtureBlockStates(FixtureBlockStates& states) {
    const std::array<std::pair<std::string_view, BlockStateId*>, 15u> required{{
        {kAirBlockId, &states.air},
        {kStoneBlockId, &states.stone},
        {kOakPlanksBlockId, &states.oakPlanks},
        {kSprucePlanksBlockId, &states.sprucePlanks},
        {kGlassBlockId, &states.glass},
        {kTorchBlockId, &states.torch},
        {kGlowstoneBlockId, &states.glowstone},
        {kMagmaBlockId, &states.magma},
        {kIronBlockId, &states.iron},
        {kGoldBlockId, &states.gold},
        {kDiamondBlockId, &states.diamond},
        {kGrassBlockId, &states.grass},
        {kOakLogBlockId, &states.oakLog},
        {kOakLeavesBlockId, &states.oakLeaves},
        {kTallGrassBlockId, &states.tallGrass},
    }};
    for (const auto& entry : required) {
        if (!resolveBlockState(entry.first, *entry.second)) {
            return {ValidationVoxelFixtureError::MissingBlockDefinition, std::string(entry.first)};
        }
    }
    return {};
}

[[nodiscard]] BlockStateId fixtureBlockState(const FixtureBlockStates& states, const std::string_view blockId) {
    if (blockId == kAirBlockId)
        return states.air;
    if (blockId == kStoneBlockId)
        return states.stone;
    if (blockId == kOakPlanksBlockId)
        return states.oakPlanks;
    if (blockId == kSprucePlanksBlockId)
        return states.sprucePlanks;
    if (blockId == kGlassBlockId)
        return states.glass;
    if (blockId == kTorchBlockId)
        return states.torch;
    if (blockId == kGlowstoneBlockId)
        return states.glowstone;
    if (blockId == kMagmaBlockId)
        return states.magma;
    if (blockId == kIronBlockId)
        return states.iron;
    if (blockId == kGoldBlockId)
        return states.gold;
    if (blockId == kDiamondBlockId)
        return states.diamond;
    if (blockId == kGrassBlockId)
        return states.grass;
    if (blockId == kOakLogBlockId)
        return states.oakLog;
    if (blockId == kOakLeavesBlockId)
        return states.oakLeaves;
    if (blockId == kTallGrassBlockId)
        return states.tallGrass;
    std::abort();
}

[[nodiscard]] std::string positionDetail(const glm::ivec3& position) {
    return std::to_string(position.x) + "," + std::to_string(position.y) + "," + std::to_string(position.z);
}

} // namespace

bool ValidationVoxelFixtureResult::succeeded() const {
    return error == ValidationVoxelFixtureError::None;
}

std::optional<renderer::contracts::StableContentHash>
validationVoxelFixtureContentHash(const ValidationVoxelFixtureIdentity& fixture) {
    FixtureKind kind = FixtureKind::None;
    if (!fixtureKind(fixture, kind)) {
        return std::nullopt;
    }
    const std::vector<FixtureEdit> edits = buildFixtureEdits(kind);
    renderer::contracts::StableContentHashBuilder hash;
    hash.addString("mecraft.validation_voxel_fixture");
    hash.addString(fixture.id);
    hash.addUint64(fixture.version);
    for (const FixtureEdit& edit : edits) {
        hash.addInt64(edit.position.x);
        hash.addInt64(edit.position.y);
        hash.addInt64(edit.position.z);
        hash.addString(edit.blockId);
    }
    hash.addUint64(edits.size());
    return hash.value();
}

ValidationVoxelFixtureResult applyValidationVoxelFixture(World& world, const ValidationVoxelFixtureIdentity& fixture) {
    FixtureKind kind = FixtureKind::None;
    if (!fixtureKind(fixture, kind)) {
        return {ValidationVoxelFixtureError::UnsupportedFixture, fixture.id + ":" + std::to_string(fixture.version)};
    }
    FixtureBlockStates states;
    ValidationVoxelFixtureResult result = resolveFixtureBlockStates(states);
    if (!result.succeeded()) {
        return result;
    }
    const std::vector<FixtureEdit> edits = buildFixtureEdits(kind);
    for (const FixtureEdit& edit : edits) {
        if (!world.isChunkLoadedForBlock(edit.position.x, edit.position.y, edit.position.z)) {
            return {ValidationVoxelFixtureError::ChunkNotLoaded, positionDetail(edit.position)};
        }
    }
    for (const FixtureEdit& edit : edits) {
        world.setBlockState(edit.position.x, edit.position.y, edit.position.z, fixtureBlockState(states, edit.blockId));
    }
    return {};
}

ValidationVoxelFixtureResult verifyValidationVoxelFixture(const IWorldView& worldView,
                                                          const ValidationVoxelFixtureIdentity& fixture) {
    FixtureKind kind = FixtureKind::None;
    if (!fixtureKind(fixture, kind)) {
        return {ValidationVoxelFixtureError::UnsupportedFixture, fixture.id + ":" + std::to_string(fixture.version)};
    }
    FixtureBlockStates states;
    ValidationVoxelFixtureResult result = resolveFixtureBlockStates(states);
    if (!result.succeeded()) {
        return result;
    }
    const std::vector<FixtureEdit> edits = buildFixtureEdits(kind);
    for (const FixtureEdit& edit : edits) {
        if (!worldView.isChunkLoadedForBlock(edit.position.x, edit.position.y, edit.position.z)) {
            return {ValidationVoxelFixtureError::ChunkNotLoaded, positionDetail(edit.position)};
        }
        if (worldView.getBlockState(edit.position.x, edit.position.y, edit.position.z) !=
            fixtureBlockState(states, edit.blockId)) {
            return {ValidationVoxelFixtureError::StateMismatch, positionDetail(edit.position)};
        }
    }
    return {};
}

const char* validationVoxelFixtureErrorStableId(const ValidationVoxelFixtureError error) {
    switch (error) {
    case ValidationVoxelFixtureError::None: return "None";
    case ValidationVoxelFixtureError::UnsupportedFixture: return "UnsupportedFixture";
    case ValidationVoxelFixtureError::MissingBlockDefinition: return "MissingBlockDefinition";
    case ValidationVoxelFixtureError::ChunkNotLoaded: return "ChunkNotLoaded";
    case ValidationVoxelFixtureError::StateMismatch: return "StateMismatch";
    }
    std::abort();
}

} // namespace app::validation
