#include <cstdlib>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "../src/world/block/Block.h"
#include "../src/world/block/BlockModelRegistry.h"
#include "../src/world/block/BlockStateRegistry.h"
#include "../src/world/block/BedBlock.h"
#include "../src/world/block/DoorBlock.h"
#include "../src/renderer/mesh/MeshBuilderRegistry.h"
#include "../src/world/fluid/FluidRegistry.h"
#include "../src/world/fluid/FluidState.h"
#include "../src/world/block/Placement.h"
#include "../src/world/block/PropIndices.h"
#include "../src/world/chunk/SubChunk.h"

namespace {
int fail(const char* message) {
    std::cerr << "[block_state_registry_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}
} // namespace

int main() {
    BlockRegistry::init(nullptr);

    if (PropIndices::FACING == PropIndices::INVALID) {
        return fail("facing property should be registered from blocks.json");
    }
    if (PropIndices::AXIS == PropIndices::INVALID) {
        return fail("axis property should be registered from blocks.json");
    }
    if (PropIndices::LEVEL == PropIndices::INVALID || PropIndices::FALLING == PropIndices::INVALID) {
        return fail("water properties should be registered from blocks.json");
    }
    if (PropIndices::SHAPE == PropIndices::INVALID || PropIndices::SHAPE_STRAIGHT == PropIndices::INVALID ||
        PropIndices::SHAPE_INNER_LEFT == PropIndices::INVALID ||
        PropIndices::SHAPE_INNER_RIGHT == PropIndices::INVALID ||
        PropIndices::SHAPE_OUTER_LEFT == PropIndices::INVALID ||
        PropIndices::SHAPE_OUTER_RIGHT == PropIndices::INVALID) {
        return fail("stair shape properties should be registered from blocks.json");
    }
    if (PropIndices::NORTH == PropIndices::INVALID || PropIndices::SOUTH == PropIndices::INVALID ||
        PropIndices::EAST == PropIndices::INVALID || PropIndices::WEST == PropIndices::INVALID) {
        return fail("connection properties should be registered from blocks.json");
    }
    if (PropIndices::PART == PropIndices::INVALID || PropIndices::PART_HEAD == PropIndices::INVALID ||
        PropIndices::PART_FOOT == PropIndices::INVALID) {
        return fail("bed part properties should be registered from blocks.json");
    }
    if (PropIndices::HALF_LOWER == PropIndices::INVALID || PropIndices::HALF_UPPER == PropIndices::INVALID ||
        PropIndices::HINGE == PropIndices::INVALID || PropIndices::HINGE_LEFT == PropIndices::INVALID ||
        PropIndices::HINGE_RIGHT == PropIndices::INVALID) {
        return fail("door half and hinge properties should be registered from blocks.json");
    }

    const BlockStateId torchDefault =
        BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:torch"));
    if (torchDefault.registryIndex() == BlockRegistry::requireIdByName("minecraft:torch")) {
        return fail("torch should expand into dedicated state ids");
    }
    if (BlockStateRegistry::getBlockId(torchDefault) != BlockRegistry::requireIdByName("minecraft:torch")) {
        return fail("expanded torch state should resolve back to torch block id");
    }
    if (BlockStateRegistry::getPropertyIndex(torchDefault, PropIndices::FACING) != PropIndices::FACING_FLOOR) {
        return fail("torch default state should face floor");
    }

    const BlockStateId torchNorth =
        BlockStateRegistry::withProperty(torchDefault, PropIndices::FACING, PropIndices::FACING_NORTH);
    if (torchNorth == torchDefault) {
        return fail("withProperty should produce a distinct torch north-facing state");
    }
    if (BlockStateRegistry::getPropertyIndex(torchNorth, PropIndices::FACING) != PropIndices::FACING_NORTH) {
        return fail("torch north-facing state should report facing=north");
    }
    const BlockID torchNorthBlock = BlockStateRegistry::getBlockId(torchNorth);
    if (!BlockRegistry::get(torchNorthBlock).isLightSource) {
        return fail("state ids should resolve explicitly to the owning block definition");
    }
    if (BlockRegistry::getBlockDropId(torchNorthBlock) !=
        BlockRegistry::getBlockDropId(BlockRegistry::requireIdByName("minecraft:torch"))) {
        return fail("state ids should explicitly reuse the owning block drop table entry");
    }

    const BlockStateId birchLogDefault =
        BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:birch_log"));
    if (BlockStateRegistry::getPropertyIndex(birchLogDefault, PropIndices::AXIS) != PropIndices::AXIS_Y) {
        return fail("birch log default state should be axis=y");
    }

    const BlockStateId birchLogX = BlockStateRegistry::getState(
        BlockRegistry::requireIdByName("minecraft:birch_log"),
        std::vector<std::pair<uint16_t, uint16_t>>{{PropIndices::AXIS, PropIndices::AXIS_X}});
    if (birchLogX == birchLogDefault) {
        return fail("birch log x-axis state should differ from the default state");
    }
    if (BlockStateRegistry::getPropertyIndex(birchLogX, PropIndices::AXIS) != PropIndices::AXIS_X) {
        return fail("birch log x-axis state should report axis=x");
    }

    const StateTextureIndices& birchLogDefaultTextures = BlockStateRegistry::getStateTextures(birchLogDefault);
    const StateTextureIndices& birchLogXTextures = BlockStateRegistry::getStateTextures(birchLogX);
    if (birchLogXTextures.faceLeft.firstLayer != birchLogDefaultTextures.faceTop.firstLayer ||
        birchLogXTextures.faceRight.firstLayer != birchLogDefaultTextures.faceTop.firstLayer) {
        return fail("birch log x-axis state should rotate end-grain textures onto left/right faces");
    }
    if (birchLogXTextures.faceTop.firstLayer != birchLogDefaultTextures.faceFront.firstLayer ||
        birchLogXTextures.faceBottom.firstLayer != birchLogDefaultTextures.faceFront.firstLayer) {
        return fail("birch log x-axis state should rotate bark textures onto top/bottom faces");
    }

    const BlockID furnace = BlockRegistry::findByName("furnace");
    if (furnace == RUNTIME_ID_NULL) {
        return fail("furnace should be registered from blocks.json");
    }
    const BlockDef& furnaceDef = BlockRegistry::get(furnace);
    if (furnaceDef.containerUi != "minecraft:furnace") {
        return fail("furnace should parse its container UI binding from blocks.json");
    }
    if (furnaceDef.placementStrategy != "horizontal_facing") {
        return fail("furnace should parse horizontal_facing placement strategy");
    }
    if (!furnaceDef.revertPlacementFacing) {
        return fail("furnace should face back toward the placing player");
    }
    const BlockStateId furnaceSouth =
        BlockStateRegistry::getState(furnace, PropIndices::FACING, PropIndices::FACING_SOUTH);
    const BlockStateId furnaceNorth =
        BlockStateRegistry::getState(furnace, PropIndices::FACING, PropIndices::FACING_NORTH);
    const BlockStateId furnaceEast =
        BlockStateRegistry::getState(furnace, PropIndices::FACING, PropIndices::FACING_EAST);
    const BlockStateId furnaceWest =
        BlockStateRegistry::getState(furnace, PropIndices::FACING, PropIndices::FACING_WEST);
    const StateTextureIndices& furnaceSouthTextures = BlockStateRegistry::getStateTextures(furnaceSouth);
    const StateTextureIndices& furnaceNorthTextures = BlockStateRegistry::getStateTextures(furnaceNorth);
    const StateTextureIndices& furnaceEastTextures = BlockStateRegistry::getStateTextures(furnaceEast);
    const StateTextureIndices& furnaceWestTextures = BlockStateRegistry::getStateTextures(furnaceWest);
    const int furnaceFrontLayer = furnaceSouthTextures.faceFront.firstLayer;
    const int furnaceSideLayer = furnaceSouthTextures.faceLeft.firstLayer;
    if (furnaceNorthTextures.faceBack.firstLayer != furnaceFrontLayer ||
        furnaceNorthTextures.faceFront.firstLayer != furnaceSideLayer) {
        return fail("north-facing furnace should place the front texture on the back face");
    }
    if (furnaceEastTextures.faceRight.firstLayer != furnaceFrontLayer ||
        furnaceEastTextures.faceFront.firstLayer != furnaceSideLayer) {
        return fail("east-facing furnace should place the front texture on the right face");
    }
    if (furnaceWestTextures.faceLeft.firstLayer != furnaceFrontLayer ||
        furnaceWestTextures.faceFront.firstLayer != furnaceSideLayer) {
        return fail("west-facing furnace should place the front texture on the left face");
    }
    PlacementStrategyFn furnaceStrategy = PlacementStrategyRegistry::getStrategy(furnaceDef.placementStrategy);
    if (furnaceStrategy == nullptr) {
        return fail("furnace placement strategy should be registered");
    }
    PlacementContext furnacePlacement;
    furnacePlacement.blockId = furnace;
    furnacePlacement.playerYaw = 270.0f;
    const BlockStateId furnacePlacedSouth = furnaceStrategy(furnacePlacement);
    if (BlockStateRegistry::getPropertyIndex(furnacePlacedSouth, PropIndices::FACING) != PropIndices::FACING_SOUTH) {
        return fail("furnace placement should face back toward the placing player");
    }

    const std::string torchStateString = BlockStateRegistry::stateToString(torchNorth);
    if (torchStateString.find("facing=north") == std::string::npos) {
        return fail("stateToString should include the resolved torch facing");
    }

    const BlockDef& torchDef = BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:torch"));
    if (torchDef.placementStrategy != "attach_wall") {
        return fail("torch should parse placementStrategy from blocks.json");
    }
    if (torchDef.revertPlacementFacing) {
        return fail("torch should keep the default non-reverted placement facing semantics");
    }
    if (torchDef.supportRule != "attached_face") {
        return fail("torch should parse supportRule from blocks.json");
    }
    if (torchDef.renderShapeName != "torch") {
        return fail("torch should parse its registered render shape name from blocks.json");
    }

    const BlockDef& birchLogDef = BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:birch_log"));
    if (birchLogDef.placementStrategy != "axis_oriented") {
        return fail("birch log should parse axis-oriented placement strategy");
    }

    const BlockID oakStairs = BlockRegistry::findByName("oak_stairs");
    if (oakStairs == RUNTIME_ID_NULL) {
        return fail("oak_stairs should be registered from blocks.json");
    }
    const BlockDef& oakStairsDef = BlockRegistry::get(oakStairs);
    if (oakStairsDef.renderShapeName != "model" ||
        oakStairsDef.renderShapeTag != MeshBuilderRegistry::getShapeTag("model")) {
        return fail("oak_stairs should use the model mesh builder");
    }
    if (!oakStairsDef.revertPlacementFacing) {
        return fail("oak_stairs should parse placement facing revert from blocks.json");
    }
    const BlockStateId oakStairsSouth = BlockStateRegistry::getState(
        oakStairs, std::vector<std::pair<uint16_t, uint16_t>>{{PropIndices::FACING, PropIndices::FACING_SOUTH},
                                                              {PropIndices::HALF, PropIndices::HALF_BOTTOM},
                                                              {PropIndices::SHAPE, PropIndices::SHAPE_STRAIGHT}});
    const ModelVariant* oakStairsVariant = BlockStateRegistry::getModelVariant(oakStairsSouth);
    if (oakStairsVariant == nullptr || oakStairsVariant->model == nullptr ||
        oakStairsVariant->model->name != "block/oak_stairs" || oakStairsVariant->transform.rotY != 90 ||
        !oakStairsVariant->transform.uvLock) {
        return fail("oak_stairs south state should resolve to the rotated oak stairs model");
    }

    const std::vector<std::tuple<uint16_t, uint16_t, uint16_t, const char*, uint16_t, uint16_t, bool>>
        oakStairsVariantCases = {
            {PropIndices::FACING_EAST, PropIndices::HALF_BOTTOM, PropIndices::SHAPE_STRAIGHT, "block/oak_stairs", 0, 0,
             false},
            {PropIndices::FACING_EAST, PropIndices::HALF_BOTTOM, PropIndices::SHAPE_INNER_LEFT,
             "block/oak_stairs_inner", 270, 0, true},
            {PropIndices::FACING_EAST, PropIndices::HALF_BOTTOM, PropIndices::SHAPE_INNER_RIGHT,
             "block/oak_stairs_inner", 0, 0, false},
            {PropIndices::FACING_EAST, PropIndices::HALF_BOTTOM, PropIndices::SHAPE_OUTER_LEFT,
             "block/oak_stairs_outer", 270, 0, true},
            {PropIndices::FACING_EAST, PropIndices::HALF_BOTTOM, PropIndices::SHAPE_OUTER_RIGHT,
             "block/oak_stairs_outer", 0, 0, false},
            {PropIndices::FACING_SOUTH, PropIndices::HALF_BOTTOM, PropIndices::SHAPE_STRAIGHT, "block/oak_stairs", 90,
             0, true},
            {PropIndices::FACING_SOUTH, PropIndices::HALF_BOTTOM, PropIndices::SHAPE_INNER_LEFT,
             "block/oak_stairs_inner", 0, 0, false},
            {PropIndices::FACING_SOUTH, PropIndices::HALF_BOTTOM, PropIndices::SHAPE_INNER_RIGHT,
             "block/oak_stairs_inner", 90, 0, true},
            {PropIndices::FACING_SOUTH, PropIndices::HALF_BOTTOM, PropIndices::SHAPE_OUTER_LEFT,
             "block/oak_stairs_outer", 0, 0, false},
            {PropIndices::FACING_SOUTH, PropIndices::HALF_BOTTOM, PropIndices::SHAPE_OUTER_RIGHT,
             "block/oak_stairs_outer", 90, 0, true},
            {PropIndices::FACING_WEST, PropIndices::HALF_BOTTOM, PropIndices::SHAPE_STRAIGHT, "block/oak_stairs", 180,
             0, true},
            {PropIndices::FACING_WEST, PropIndices::HALF_BOTTOM, PropIndices::SHAPE_INNER_LEFT,
             "block/oak_stairs_inner", 90, 0, true},
            {PropIndices::FACING_WEST, PropIndices::HALF_BOTTOM, PropIndices::SHAPE_INNER_RIGHT,
             "block/oak_stairs_inner", 180, 0, true},
            {PropIndices::FACING_WEST, PropIndices::HALF_BOTTOM, PropIndices::SHAPE_OUTER_LEFT,
             "block/oak_stairs_outer", 90, 0, true},
            {PropIndices::FACING_WEST, PropIndices::HALF_BOTTOM, PropIndices::SHAPE_OUTER_RIGHT,
             "block/oak_stairs_outer", 180, 0, true},
            {PropIndices::FACING_NORTH, PropIndices::HALF_BOTTOM, PropIndices::SHAPE_STRAIGHT, "block/oak_stairs", 270,
             0, true},
            {PropIndices::FACING_NORTH, PropIndices::HALF_BOTTOM, PropIndices::SHAPE_INNER_LEFT,
             "block/oak_stairs_inner", 180, 0, true},
            {PropIndices::FACING_NORTH, PropIndices::HALF_BOTTOM, PropIndices::SHAPE_INNER_RIGHT,
             "block/oak_stairs_inner", 270, 0, true},
            {PropIndices::FACING_NORTH, PropIndices::HALF_BOTTOM, PropIndices::SHAPE_OUTER_LEFT,
             "block/oak_stairs_outer", 180, 0, true},
            {PropIndices::FACING_NORTH, PropIndices::HALF_BOTTOM, PropIndices::SHAPE_OUTER_RIGHT,
             "block/oak_stairs_outer", 270, 0, true},
            {PropIndices::FACING_EAST, PropIndices::HALF_TOP, PropIndices::SHAPE_STRAIGHT, "block/oak_stairs", 0, 180,
             true},
            {PropIndices::FACING_EAST, PropIndices::HALF_TOP, PropIndices::SHAPE_INNER_LEFT, "block/oak_stairs_inner",
             270, 180, true},
            {PropIndices::FACING_EAST, PropIndices::HALF_TOP, PropIndices::SHAPE_INNER_RIGHT, "block/oak_stairs_inner",
             0, 180, true},
            {PropIndices::FACING_EAST, PropIndices::HALF_TOP, PropIndices::SHAPE_OUTER_LEFT, "block/oak_stairs_outer",
             270, 180, true},
            {PropIndices::FACING_EAST, PropIndices::HALF_TOP, PropIndices::SHAPE_OUTER_RIGHT, "block/oak_stairs_outer",
             0, 180, true},
            {PropIndices::FACING_SOUTH, PropIndices::HALF_TOP, PropIndices::SHAPE_STRAIGHT, "block/oak_stairs", 90, 180,
             true},
            {PropIndices::FACING_SOUTH, PropIndices::HALF_TOP, PropIndices::SHAPE_INNER_LEFT, "block/oak_stairs_inner",
             0, 180, true},
            {PropIndices::FACING_SOUTH, PropIndices::HALF_TOP, PropIndices::SHAPE_INNER_RIGHT, "block/oak_stairs_inner",
             90, 180, true},
            {PropIndices::FACING_SOUTH, PropIndices::HALF_TOP, PropIndices::SHAPE_OUTER_LEFT, "block/oak_stairs_outer",
             0, 180, true},
            {PropIndices::FACING_SOUTH, PropIndices::HALF_TOP, PropIndices::SHAPE_OUTER_RIGHT, "block/oak_stairs_outer",
             90, 180, true},
            {PropIndices::FACING_WEST, PropIndices::HALF_TOP, PropIndices::SHAPE_STRAIGHT, "block/oak_stairs", 180, 180,
             true},
            {PropIndices::FACING_WEST, PropIndices::HALF_TOP, PropIndices::SHAPE_INNER_LEFT, "block/oak_stairs_inner",
             90, 180, true},
            {PropIndices::FACING_WEST, PropIndices::HALF_TOP, PropIndices::SHAPE_INNER_RIGHT, "block/oak_stairs_inner",
             180, 180, true},
            {PropIndices::FACING_WEST, PropIndices::HALF_TOP, PropIndices::SHAPE_OUTER_LEFT, "block/oak_stairs_outer",
             90, 180, true},
            {PropIndices::FACING_WEST, PropIndices::HALF_TOP, PropIndices::SHAPE_OUTER_RIGHT, "block/oak_stairs_outer",
             180, 180, true},
            {PropIndices::FACING_NORTH, PropIndices::HALF_TOP, PropIndices::SHAPE_STRAIGHT, "block/oak_stairs", 270,
             180, true},
            {PropIndices::FACING_NORTH, PropIndices::HALF_TOP, PropIndices::SHAPE_INNER_LEFT, "block/oak_stairs_inner",
             180, 180, true},
            {PropIndices::FACING_NORTH, PropIndices::HALF_TOP, PropIndices::SHAPE_INNER_RIGHT, "block/oak_stairs_inner",
             270, 180, true},
            {PropIndices::FACING_NORTH, PropIndices::HALF_TOP, PropIndices::SHAPE_OUTER_LEFT, "block/oak_stairs_outer",
             180, 180, true},
            {PropIndices::FACING_NORTH, PropIndices::HALF_TOP, PropIndices::SHAPE_OUTER_RIGHT, "block/oak_stairs_outer",
             270, 180, true},
        };
    for (const auto& [facing, half, shape, expectedModel, expectedRotY, expectedRotX, expectedUvLock] :
         oakStairsVariantCases) {
        const BlockStateId state = BlockStateRegistry::getState(
            oakStairs, std::vector<std::pair<uint16_t, uint16_t>>{
                           {PropIndices::FACING, facing}, {PropIndices::HALF, half}, {PropIndices::SHAPE, shape}});
        const ModelVariant* variant = BlockStateRegistry::getModelVariant(state);
        if (variant == nullptr || variant->model == nullptr || variant->model->name != expectedModel ||
            variant->transform.rotY != expectedRotY || variant->transform.rotX != expectedRotX ||
            variant->transform.uvLock != expectedUvLock) {
            return fail("all oak_stairs facing/half/shape states should resolve to their JSON model transforms");
        }
    }

    const BlockID redBed = BlockRegistry::findByName("red_bed");
    if (redBed == RUNTIME_ID_NULL) {
        return fail("red_bed should be registered from blocks.json");
    }
    const BlockDef& redBedDef = BlockRegistry::get(redBed);
    if (redBedDef.renderShapeName != "model" || redBedDef.renderShapeTag != MeshBuilderRegistry::getShapeTag("model")) {
        return fail("red_bed should use the model mesh builder");
    }
    if (redBedDef.placementStrategy != "bed" || redBedDef.isSolid || redBedDef.isTransparent ||
        redBedDef.renderLayer != BlockRenderLayer::Opaque || redBedDef.opacity != 0) {
        return fail("red_bed should parse bed placement and opaque non-full-block properties");
    }
    if (!BedBlockLogic::isBedBlock(redBed)) {
        return fail("red_bed should be recognized by bed block logic");
    }
    if (std::is_convertible<BlockID, BlockStateId>::value) {
        return fail("raw block ids should not implicitly convert to complete block states");
    }

    const BlockStateId redBedDefault = BlockStateRegistry::getDefaultState(redBed);
    if (BlockStateRegistry::getBlockId(redBedDefault) != redBed ||
        BlockStateRegistry::getPropertyIndex(redBedDefault, PropIndices::FACING) != PropIndices::FACING_EAST ||
        BlockStateRegistry::getPropertyIndex(redBedDefault, PropIndices::PART) != PropIndices::PART_FOOT ||
        !BedBlockLogic::isFootState(redBedDefault)) {
        return fail("red_bed default state should be east-facing foot");
    }

    const BlockStateId redBedHead =
        BedBlockLogic::makeBedState(redBed, PropIndices::FACING_EAST, PropIndices::PART_HEAD);
    if (!BedBlockLogic::isHeadState(redBedHead) ||
        BlockStateRegistry::getPropertyIndex(redBedHead, PropIndices::PART) != PropIndices::PART_HEAD) {
        return fail("bed helper should construct head states");
    }
    const ModelVariant* redBedFootVariant = BlockStateRegistry::getModelVariant(redBedDefault);
    const ModelVariant* redBedHeadVariant = BlockStateRegistry::getModelVariant(redBedHead);
    if (redBedFootVariant == nullptr || redBedFootVariant->model == nullptr || redBedHeadVariant == nullptr ||
        redBedHeadVariant->model == nullptr || redBedFootVariant->model->elements.empty() ||
        redBedHeadVariant->model->elements.empty()) {
        return fail("red_bed foot and head states should resolve to model elements");
    }
    const auto bedBodyUvMatches = [](const std::unique_ptr<ModelFace>& face) {
        return face != nullptr && face->uv[0] == 0.0f && face->uv[1] == 3.0f && face->uv[2] == 16.0f &&
               face->uv[3] == 9.0f;
    };
    const auto bedBodyNorthUvMatches = [](const std::unique_ptr<ModelFace>& face) {
        return face != nullptr && face->uv[0] == 16.0f && face->uv[1] == 3.0f && face->uv[2] == 0.0f &&
               face->uv[3] == 9.0f;
    };
    if (!bedBodyNorthUvMatches(redBedFootVariant->model->elements.front().faces[3]) ||
        !bedBodyUvMatches(redBedFootVariant->model->elements.front().faces[4]) ||
        !bedBodyNorthUvMatches(redBedHeadVariant->model->elements.front().faces[3]) ||
        !bedBodyUvMatches(redBedHeadVariant->model->elements.front().faces[5])) {
        return fail("red_bed body side and end faces should use the visible strip of the legacy bed textures");
    }
    const glm::ivec3 eastHeadOffset = BedBlockLogic::headOffsetForFacing(PropIndices::FACING_EAST);
    if (eastHeadOffset.x != 1 || eastHeadOffset.y != 0 || eastHeadOffset.z != 0) {
        return fail("east-facing bed head offset should point east");
    }

    const std::vector<std::tuple<uint16_t, uint16_t, const char*, uint16_t, bool>> bedVariantCases = {
        {PropIndices::FACING_EAST, PropIndices::PART_FOOT, "block/bed_foot", 0, false},
        {PropIndices::FACING_EAST, PropIndices::PART_HEAD, "block/bed_head", 0, false},
        {PropIndices::FACING_SOUTH, PropIndices::PART_FOOT, "block/bed_foot", 90, false},
        {PropIndices::FACING_SOUTH, PropIndices::PART_HEAD, "block/bed_head", 90, false},
        {PropIndices::FACING_WEST, PropIndices::PART_FOOT, "block/bed_foot", 180, false},
        {PropIndices::FACING_WEST, PropIndices::PART_HEAD, "block/bed_head", 180, false},
        {PropIndices::FACING_NORTH, PropIndices::PART_FOOT, "block/bed_foot", 270, false},
        {PropIndices::FACING_NORTH, PropIndices::PART_HEAD, "block/bed_head", 270, false},
    };
    for (const auto& [facing, part, expectedModel, expectedRotY, expectedUvLock] : bedVariantCases) {
        const BlockStateId state = BlockStateRegistry::getState(
            redBed,
            std::vector<std::pair<uint16_t, uint16_t>>{{PropIndices::FACING, facing}, {PropIndices::PART, part}});
        const ModelVariant* variant = BlockStateRegistry::getModelVariant(state);
        if (variant == nullptr || variant->model == nullptr || variant->model->name != expectedModel ||
            variant->transform.rotY != expectedRotY || variant->transform.rotX != 0 ||
            variant->transform.uvLock != expectedUvLock) {
            return fail("all red_bed facing/part states should resolve to their JSON model transforms");
        }
    }

    PlacementStrategyFn bedStrategy = PlacementStrategyRegistry::getStrategy(redBedDef.placementStrategy);
    if (bedStrategy == nullptr) {
        return fail("bed placement strategy should be registered");
    }
    PlacementContext bedPlacement;
    bedPlacement.blockId = redBed;
    bedPlacement.playerYaw = 180.0f;
    const BlockStateId redBedPlacedWest = bedStrategy(bedPlacement);
    if (BlockStateRegistry::getPropertyIndex(redBedPlacedWest, PropIndices::FACING) != PropIndices::FACING_WEST ||
        BlockStateRegistry::getPropertyIndex(redBedPlacedWest, PropIndices::PART) != PropIndices::PART_FOOT) {
        return fail("bed placement should create a west-facing foot state from player yaw");
    }
    glm::ivec3 otherHalfPos{};
    if (!BedBlockLogic::tryGetOtherHalfPosition(glm::ivec3(10, 64, 10), redBedPlacedWest, otherHalfPos) ||
        otherHalfPos.x != 9 || otherHalfPos.y != 64 || otherHalfPos.z != 10) {
        return fail("bed helper should locate the head from a west-facing foot");
    }

    const BlockID oakSlab = BlockRegistry::findByName("oak_slab");
    if (oakSlab == RUNTIME_ID_NULL) {
        return fail("oak_slab should be registered from blocks.json");
    }
    const BlockDef& oakSlabDef = BlockRegistry::get(oakSlab);
    if (oakSlabDef.placementStrategy != "slab") {
        return fail("oak_slab should parse slab placement strategy");
    }
    if (PropIndices::HALF_DOUBLE == PropIndices::INVALID) {
        return fail("half=double should be registered for stacked slabs");
    }
    const BlockStateId oakSlabTop = BlockStateRegistry::getState(
        oakSlab, std::vector<std::pair<uint16_t, uint16_t>>{{PropIndices::HALF, PropIndices::HALF_TOP}});
    const ModelVariant* oakSlabVariant = BlockStateRegistry::getModelVariant(oakSlabTop);
    if (oakSlabVariant == nullptr || oakSlabVariant->model == nullptr ||
        oakSlabVariant->model->name != "block/oak_slab_top") {
        return fail("oak_slab top state should resolve to the top slab model");
    }
    const BlockStateId oakSlabDouble = BlockStateRegistry::getState(
        oakSlab, std::vector<std::pair<uint16_t, uint16_t>>{{PropIndices::HALF, PropIndices::HALF_DOUBLE}});
    const ModelVariant* oakSlabDoubleVariant = BlockStateRegistry::getModelVariant(oakSlabDouble);
    if (oakSlabDoubleVariant == nullptr || oakSlabDoubleVariant->model == nullptr ||
        oakSlabDoubleVariant->model->name != "block/oak_slab_double") {
        return fail("oak_slab double state should resolve to the full slab model");
    }

    const BlockID oakVerticalSlab = BlockRegistry::findByName("oak_vertical_slab");
    if (oakVerticalSlab == RUNTIME_ID_NULL) {
        return fail("oak_vertical_slab should be registered from blocks.json");
    }
    const BlockDef& oakVerticalSlabDef = BlockRegistry::get(oakVerticalSlab);
    if (oakVerticalSlabDef.placementStrategy != "vertical_slab") {
        return fail("oak_vertical_slab should parse vertical slab placement strategy");
    }
    if (PropIndices::HALF_NORTH == PropIndices::INVALID || PropIndices::HALF_SOUTH == PropIndices::INVALID ||
        PropIndices::HALF_EAST == PropIndices::INVALID || PropIndices::HALF_WEST == PropIndices::INVALID) {
        return fail("horizontal half values should be registered for vertical slabs");
    }
    const BlockStateId oakVerticalSlabEast = BlockStateRegistry::getState(
        oakVerticalSlab, std::vector<std::pair<uint16_t, uint16_t>>{{PropIndices::HALF, PropIndices::HALF_EAST}});
    const ModelVariant* oakVerticalSlabEastVariant = BlockStateRegistry::getModelVariant(oakVerticalSlabEast);
    if (oakVerticalSlabEastVariant == nullptr || oakVerticalSlabEastVariant->model == nullptr ||
        oakVerticalSlabEastVariant->model->name != "block/oak_vertical_slab_east") {
        return fail("oak_vertical_slab east state should resolve to the east vertical slab model");
    }
    const BlockStateId oakVerticalSlabWest = BlockStateRegistry::getState(
        oakVerticalSlab, std::vector<std::pair<uint16_t, uint16_t>>{{PropIndices::HALF, PropIndices::HALF_WEST}});
    const BlockStateId oakVerticalSlabNorth = BlockStateRegistry::getState(
        oakVerticalSlab, std::vector<std::pair<uint16_t, uint16_t>>{{PropIndices::HALF, PropIndices::HALF_NORTH}});
    const BlockStateId oakVerticalSlabSouth = BlockStateRegistry::getState(
        oakVerticalSlab, std::vector<std::pair<uint16_t, uint16_t>>{{PropIndices::HALF, PropIndices::HALF_SOUTH}});
    const BlockStateId oakVerticalSlabDouble = BlockStateRegistry::getState(
        oakVerticalSlab, std::vector<std::pair<uint16_t, uint16_t>>{{PropIndices::HALF, PropIndices::HALF_DOUBLE}});
    const ModelVariant* oakVerticalSlabDoubleVariant = BlockStateRegistry::getModelVariant(oakVerticalSlabDouble);
    if (oakVerticalSlabDoubleVariant == nullptr || oakVerticalSlabDoubleVariant->model == nullptr ||
        oakVerticalSlabDoubleVariant->model->name != "block/oak_slab_double") {
        return fail("oak_vertical_slab double state should resolve to the full slab model");
    }

    const BlockID cauldron = BlockRegistry::findByName("cauldron");
    if (cauldron == RUNTIME_ID_NULL) {
        return fail("cauldron should be registered from blocks.json");
    }
    const BlockDef& cauldronDef = BlockRegistry::get(cauldron);
    if (cauldronDef.renderShapeName != "model" ||
        cauldronDef.renderShapeTag != MeshBuilderRegistry::getShapeTag("model")) {
        return fail("cauldron should use the model mesh builder");
    }
    const BlockStateId cauldronDefault = BlockStateRegistry::getDefaultState(cauldron);
    const ModelVariant* cauldronVariant = BlockStateRegistry::getModelVariant(cauldronDefault);
    if (cauldronVariant == nullptr || cauldronVariant->model == nullptr ||
        cauldronVariant->model->name != "block/cauldron" || cauldronVariant->model->elements.size() != 13) {
        return fail("cauldron default state should resolve to the multi-element cauldron model");
    }

    const BlockID oakFence = BlockRegistry::findByName("oak_fence");
    if (oakFence == RUNTIME_ID_NULL) {
        return fail("oak_fence should be registered from blocks.json");
    }
    const BlockDef& oakFenceDef = BlockRegistry::get(oakFence);
    if (oakFenceDef.renderShapeName != "model" ||
        oakFenceDef.renderShapeTag != MeshBuilderRegistry::getShapeTag("model")) {
        return fail("oak_fence should use the model mesh builder");
    }
    if (oakFenceDef.placementStrategy != "fence") {
        return fail("oak_fence should parse fence placement strategy");
    }
    const BlockStateId oakFenceEastWest = BlockStateRegistry::getState(
        oakFence, std::vector<std::pair<uint16_t, uint16_t>>{{PropIndices::NORTH, PropIndices::NORTH_FALSE},
                                                             {PropIndices::SOUTH, PropIndices::SOUTH_FALSE},
                                                             {PropIndices::EAST, PropIndices::EAST_TRUE},
                                                             {PropIndices::WEST, PropIndices::WEST_TRUE}});
    const ModelVariant* oakFenceVariant = BlockStateRegistry::getModelVariant(oakFenceEastWest);
    if (oakFenceVariant == nullptr || oakFenceVariant->model == nullptr ||
        oakFenceVariant->model->name != "block/oak_fence_east_west" || oakFenceVariant->model->elements.size() != 5) {
        return fail("oak_fence east/west state should resolve to the connected fence model");
    }

    const BlockID cobblestoneWall = BlockRegistry::findByName("cobblestone_wall");
    if (cobblestoneWall == RUNTIME_ID_NULL) {
        return fail("cobblestone_wall should be registered from blocks.json");
    }
    const BlockDef& cobblestoneWallDef = BlockRegistry::get(cobblestoneWall);
    if (cobblestoneWallDef.renderShapeName != "model" ||
        cobblestoneWallDef.renderShapeTag != MeshBuilderRegistry::getShapeTag("model")) {
        return fail("cobblestone_wall should use the model mesh builder");
    }
    if (cobblestoneWallDef.placementStrategy != "wall") {
        return fail("cobblestone_wall should parse wall placement strategy");
    }
    const BlockStateId cobblestoneWallNorthSouth = BlockStateRegistry::getState(
        cobblestoneWall, std::vector<std::pair<uint16_t, uint16_t>>{{PropIndices::NORTH, PropIndices::NORTH_TRUE},
                                                                    {PropIndices::SOUTH, PropIndices::SOUTH_TRUE},
                                                                    {PropIndices::EAST, PropIndices::EAST_FALSE},
                                                                    {PropIndices::WEST, PropIndices::WEST_FALSE}});
    const ModelVariant* cobblestoneWallVariant = BlockStateRegistry::getModelVariant(cobblestoneWallNorthSouth);
    if (cobblestoneWallVariant == nullptr || cobblestoneWallVariant->model == nullptr ||
        cobblestoneWallVariant->model->name != "block/cobblestone_wall_north_south" ||
        cobblestoneWallVariant->model->elements.size() != 3) {
        return fail("cobblestone_wall north/south state should resolve to the connected wall model");
    }

    const BlockID anvil = BlockRegistry::findByName("anvil");
    if (anvil == RUNTIME_ID_NULL) {
        return fail("anvil should be registered from blocks.json");
    }
    const BlockDef& anvilDef = BlockRegistry::get(anvil);
    if (anvilDef.renderShapeName != "model" || anvilDef.renderShapeTag != MeshBuilderRegistry::getShapeTag("model")) {
        return fail("anvil should use the model mesh builder");
    }
    if (anvilDef.placementStrategy != "horizontal_facing") {
        return fail("anvil should parse horizontal_facing placement strategy");
    }
    const BlockStateId anvilEast = BlockStateRegistry::getState(
        anvil, std::vector<std::pair<uint16_t, uint16_t>>{{PropIndices::FACING, PropIndices::FACING_EAST}});
    const ModelVariant* anvilVariant = BlockStateRegistry::getModelVariant(anvilEast);
    if (anvilVariant == nullptr || anvilVariant->model == nullptr || anvilVariant->model->name != "block/anvil" ||
        anvilVariant->model->elements.size() != 4 || anvilVariant->transform.rotY != 90) {
        return fail("anvil east state should resolve to the rotated anvil model");
    }

    PlacementStrategyFn torchStrategy = PlacementStrategyRegistry::getStrategy(torchDef.placementStrategy);
    if (torchStrategy == nullptr) {
        return fail("torch placement strategy should be registered");
    }

    PlacementContext torchPlacement;
    torchPlacement.blockId = BlockRegistry::requireIdByName("minecraft:torch");
    torchPlacement.hitNormal = {0, 0, -1};
    const BlockStateId torchPlacedNorth = torchStrategy(torchPlacement);
    if (BlockStateRegistry::getPropertyIndex(torchPlacedNorth, PropIndices::FACING) != PropIndices::FACING_NORTH) {
        return fail("attach_wall placement should derive torch facing from hit normal");
    }

    PlacementStrategyFn logStrategy = PlacementStrategyRegistry::getStrategy(birchLogDef.placementStrategy);
    if (logStrategy == nullptr) {
        return fail("birch log placement strategy should be registered");
    }

    PlacementContext logPlacement;
    logPlacement.blockId = BlockRegistry::requireIdByName("minecraft:birch_log");
    logPlacement.hitNormal = {1, 0, 0};
    const BlockStateId birchLogPlacedX = logStrategy(logPlacement);
    if (BlockStateRegistry::getPropertyIndex(birchLogPlacedX, PropIndices::AXIS) != PropIndices::AXIS_X) {
        return fail("axis_oriented placement should derive log axis from hit normal");
    }

    PlacementStrategyFn slabStrategy = PlacementStrategyRegistry::getStrategy(oakSlabDef.placementStrategy);
    if (slabStrategy == nullptr) {
        return fail("slab placement strategy should be registered");
    }

    PlacementContext slabBottomPlacement;
    slabBottomPlacement.blockId = oakSlab;
    slabBottomPlacement.hitNormal = {0, 1, 0};
    const BlockStateId slabPlacedBottom = slabStrategy(slabBottomPlacement);
    if (BlockStateRegistry::getPropertyIndex(slabPlacedBottom, PropIndices::HALF) != PropIndices::HALF_BOTTOM) {
        return fail("slab placement should place bottom slabs on upward-facing hits");
    }

    PlacementContext slabTopPlacement;
    slabTopPlacement.blockId = oakSlab;
    slabTopPlacement.hitNormal = {0, -1, 0};
    const BlockStateId slabPlacedTop = slabStrategy(slabTopPlacement);
    if (BlockStateRegistry::getPropertyIndex(slabPlacedTop, PropIndices::HALF) != PropIndices::HALF_TOP) {
        return fail("slab placement should place top slabs on downward-facing hits");
    }

    BlockStateId slabMerged = NULL_BLOCK_STATE;
    if (!tryMergePlacementStates(slabPlacedBottom, slabPlacedTop, slabMerged) || slabMerged != oakSlabDouble) {
        return fail("bottom and top slab placement states should merge into a double slab");
    }
    if (!canReplaceWithMergedPlacementResult(slabPlacedBottom, oakSlabDouble)) {
        return fail("server placement validation should accept legal slab merge results");
    }
    if (canReplaceWithMergedPlacementResult(slabPlacedBottom, slabPlacedTop)) {
        return fail("server placement validation should reject non-merged occupied slab replacement");
    }

    PlacementStrategyFn verticalSlabStrategy =
        PlacementStrategyRegistry::getStrategy(oakVerticalSlabDef.placementStrategy);
    if (verticalSlabStrategy == nullptr) {
        return fail("vertical slab placement strategy should be registered");
    }

    PlacementContext verticalSlabSidePlacement;
    verticalSlabSidePlacement.blockId = oakVerticalSlab;
    verticalSlabSidePlacement.hitNormal = {1, 0, 0};
    const BlockStateId verticalSlabSidePlaced = verticalSlabStrategy(verticalSlabSidePlacement);
    if (BlockStateRegistry::getPropertyIndex(verticalSlabSidePlaced, PropIndices::HALF) != PropIndices::HALF_WEST) {
        return fail("vertical slab side placement should occupy the half touching the clicked face");
    }

    PlacementContext verticalSlabTopPlacement;
    verticalSlabTopPlacement.blockId = oakVerticalSlab;
    verticalSlabTopPlacement.hitNormal = {0, 1, 0};
    verticalSlabTopPlacement.hitPosition = {12.75f, 64.0f, 8.5f};
    const BlockStateId verticalSlabTopPlaced = verticalSlabStrategy(verticalSlabTopPlacement);
    if (BlockStateRegistry::getPropertyIndex(verticalSlabTopPlaced, PropIndices::HALF) != PropIndices::HALF_EAST) {
        return fail("vertical slab top placement should use hit position to select the east half");
    }

    verticalSlabTopPlacement.isSneaking = true;
    const BlockStateId verticalSlabSneakPlaced = verticalSlabStrategy(verticalSlabTopPlacement);
    if (BlockStateRegistry::getPropertyIndex(verticalSlabSneakPlaced, PropIndices::HALF) != PropIndices::HALF_WEST) {
        return fail("sneaking vertical slab placement should invert the selected horizontal half");
    }

    BlockStateId verticalSlabMerged = NULL_BLOCK_STATE;
    if (!tryMergePlacementStates(oakVerticalSlabEast, oakVerticalSlabWest, verticalSlabMerged) ||
        verticalSlabMerged != oakVerticalSlabDouble) {
        return fail("east and west vertical slab states should merge into a double slab");
    }
    if (!tryMergePlacementStates(oakVerticalSlabNorth, oakVerticalSlabSouth, verticalSlabMerged) ||
        verticalSlabMerged != oakVerticalSlabDouble) {
        return fail("north and south vertical slab states should merge into a double slab");
    }
    if (tryMergePlacementStates(oakVerticalSlabNorth, oakVerticalSlabEast, verticalSlabMerged)) {
        return fail("overlapping vertical slab states should not merge");
    }

    PlacementContext slabSideTopPlacement;
    slabSideTopPlacement.blockId = oakSlab;
    slabSideTopPlacement.hitNormal = {1, 0, 0};
    slabSideTopPlacement.hitPosition = {12.0f, 64.75f, 8.5f};
    const BlockStateId slabSidePlacedTop = slabStrategy(slabSideTopPlacement);
    if (BlockStateRegistry::getPropertyIndex(slabSidePlacedTop, PropIndices::HALF) != PropIndices::HALF_TOP) {
        return fail("slab side placement should use the hit position to select the top half");
    }

    PlacementContext slabSideBottomPlacement;
    slabSideBottomPlacement.blockId = oakSlab;
    slabSideBottomPlacement.hitNormal = {-1, 0, 0};
    slabSideBottomPlacement.hitPosition = {12.0f, 64.25f, 8.5f};
    const BlockStateId slabSidePlacedBottom = slabStrategy(slabSideBottomPlacement);
    if (BlockStateRegistry::getPropertyIndex(slabSidePlacedBottom, PropIndices::HALF) != PropIndices::HALF_BOTTOM) {
        return fail("slab side placement should use the hit position to select the bottom half");
    }

    slabTopPlacement.isSneaking = true;
    const BlockStateId slabSneakPlacedBottom = slabStrategy(slabTopPlacement);
    if (BlockStateRegistry::getPropertyIndex(slabSneakPlacedBottom, PropIndices::HALF) != PropIndices::HALF_BOTTOM) {
        return fail("sneaking slab placement should invert the selected half");
    }

    PlacementStrategyFn stairsStrategy = PlacementStrategyRegistry::getStrategy(oakStairsDef.placementStrategy);
    if (stairsStrategy == nullptr) {
        return fail("stairs placement strategy should be registered");
    }

    PlacementContext stairsNorthPlacement;
    stairsNorthPlacement.blockId = oakStairs;
    stairsNorthPlacement.playerYaw = -90.0f;
    stairsNorthPlacement.hitNormal = {0, 1, 0};
    const BlockStateId stairsPlacedNorth = stairsStrategy(stairsNorthPlacement);
    if (BlockStateRegistry::getPropertyIndex(stairsPlacedNorth, PropIndices::FACING) != PropIndices::FACING_NORTH ||
        BlockStateRegistry::getPropertyIndex(stairsPlacedNorth, PropIndices::HALF) != PropIndices::HALF_BOTTOM ||
        BlockStateRegistry::getPropertyIndex(stairsPlacedNorth, PropIndices::SHAPE) != PropIndices::SHAPE_STRAIGHT) {
        return fail("stairs placement should derive north-facing bottom stairs from camera yaw and top-face hits");
    }

    PlacementContext stairsEastNorthBoundaryPlacement;
    stairsEastNorthBoundaryPlacement.blockId = oakStairs;
    stairsEastNorthBoundaryPlacement.playerYaw = 315.0f;
    stairsEastNorthBoundaryPlacement.hitNormal = {0, 1, 0};
    const BlockStateId stairsPlacedEastNorthBoundary = stairsStrategy(stairsEastNorthBoundaryPlacement);
    if (BlockStateRegistry::getPropertyIndex(stairsPlacedEastNorthBoundary, PropIndices::FACING) !=
        PropIndices::FACING_EAST) {
        return fail("stairs placement should classify the +X/-Z yaw boundary as east-facing");
    }

    PlacementContext stairsSideTopPlacement;
    stairsSideTopPlacement.blockId = oakStairs;
    stairsSideTopPlacement.playerYaw = 0.0f;
    stairsSideTopPlacement.hitNormal = {0, 0, 1};
    stairsSideTopPlacement.hitPosition = {4.5f, 32.75f, 4.0f};
    const BlockStateId stairsPlacedNorthTop = stairsStrategy(stairsSideTopPlacement);
    if (BlockStateRegistry::getPropertyIndex(stairsPlacedNorthTop, PropIndices::FACING) != PropIndices::FACING_NORTH ||
        BlockStateRegistry::getPropertyIndex(stairsPlacedNorthTop, PropIndices::HALF) != PropIndices::HALF_TOP ||
        BlockStateRegistry::getPropertyIndex(stairsPlacedNorthTop, PropIndices::SHAPE) != PropIndices::SHAPE_STRAIGHT) {
        return fail("stairs side placement should reverse the side normal for stair descent direction and derive half "
                    "from hit position");
    }

    const std::vector<std::pair<glm::ivec3, uint16_t>> stairSideCases = {
        {{1, 0, 0}, PropIndices::FACING_WEST},
        {{-1, 0, 0}, PropIndices::FACING_EAST},
        {{0, 0, 1}, PropIndices::FACING_NORTH},
        {{0, 0, -1}, PropIndices::FACING_SOUTH},
    };
    for (const auto& [normal, expectedFacing] : stairSideCases) {
        PlacementContext sidePlacement;
        sidePlacement.blockId = oakStairs;
        sidePlacement.playerYaw = 180.0f;
        sidePlacement.hitNormal = normal;
        sidePlacement.hitPosition = {4.5f, 32.25f, 4.0f};
        const BlockStateId placed = stairsStrategy(sidePlacement);
        if (BlockStateRegistry::getPropertyIndex(placed, PropIndices::FACING) != expectedFacing ||
            BlockStateRegistry::getPropertyIndex(placed, PropIndices::HALF) != PropIndices::HALF_BOTTOM ||
            BlockStateRegistry::getPropertyIndex(placed, PropIndices::SHAPE) != PropIndices::SHAPE_STRAIGHT) {
            return fail("stairs side placement should use the configured facing revert for stair descent direction");
        }
    }

    const BlockStateId chestDefault =
        BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:chest"));
    if (chestDefault.registryIndex() == BlockRegistry::requireIdByName("minecraft:chest")) {
        return fail("chest should expand into dedicated facing state ids");
    }
    if (BlockStateRegistry::getBlockId(chestDefault) != BlockRegistry::requireIdByName("minecraft:chest")) {
        return fail("expanded chest state should resolve back to chest block id");
    }
    if (BlockStateRegistry::getPropertyIndex(chestDefault, PropIndices::FACING) != PropIndices::FACING_SOUTH) {
        return fail("chest default state should face south");
    }

    const BlockDef& chestDef = BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:chest"));
    if (chestDef.renderShapeName != "block_entity" ||
        chestDef.renderShapeTag != MeshBuilderRegistry::getShapeTag("block_entity")) {
        return fail("chest should render through the block entity renderer shape");
    }
    if (!chestDef.isSolid || !chestDef.isTransparent || chestDef.opacity != 0) {
        return fail("chest should keep collision while allowing neighboring terrain faces and light");
    }
    if (chestDef.placementStrategy != "horizontal_facing") {
        return fail("chest should parse horizontal_facing placement strategy");
    }
    if (!chestDef.revertPlacementFacing) {
        return fail("chest should parse placement facing revert from blocks.json");
    }
    if (chestDef.containerUi != "minecraft:chest") {
        return fail("chest should parse its container UI binding from blocks.json");
    }

    const BlockID barrel = BlockRegistry::requireIdByName("minecraft:barrel");
    const BlockDef& barrelDef = BlockRegistry::get(barrel);
    if (barrelDef.containerUi != "minecraft:barrel") {
        return fail("barrel should parse its container UI binding from blocks.json");
    }
    if (!barrelDef.isSolid || barrelDef.isTransparent || barrelDef.materialKind != BlockMaterialKinds::WOOD) {
        return fail("barrel should be a solid wooden storage block");
    }
    if (!std::filesystem::exists("assets/textures/blocks/barrel_top.png") ||
        !std::filesystem::exists("assets/textures/blocks/barrel_bottom.png") ||
        !std::filesystem::exists("assets/textures/blocks/barrel_side.png")) {
        return fail("barrel should provide top, bottom, and side block textures");
    }

    const BlockDef& craftingTableDef = BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:crafting_table"));
    if (craftingTableDef.containerUi != "minecraft:crafting_table") {
        return fail("crafting table should parse its container UI binding from blocks.json");
    }

    const BlockDef& stoneButtonPulseDef = BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:stone_button"));
    const BlockDef& oakButtonPulseDef = BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:oak_button"));
    if (stoneButtonPulseDef.redstonePulseTicks != 10 || oakButtonPulseDef.redstonePulseTicks != 15) {
        return fail("button pulse durations should parse from blocks.json");
    }

    const BlockDef& stonePressurePlateDef =
        BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:stone_pressure_plate"));
    const BlockDef& oakPressurePlateDef =
        BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:oak_pressure_plate"));
    if (stonePressurePlateDef.pressurePlateEntityFilter != "living" ||
        oakPressurePlateDef.pressurePlateEntityFilter != "all") {
        return fail("pressure plate entity filters should parse from blocks.json");
    }

    const BlockDef& bedrockDef = BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:bedrock"));
    const BlockDef& obsidianDef = BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:obsidian"));
    const BlockDef& pistonHeadDef = BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:piston_head"));
    if (bedrockDef.pistonPushReaction != "block" || obsidianDef.pistonPushReaction != "block" ||
        pistonHeadDef.pistonPushReaction != "block") {
        return fail("immovable piston blocks should parse their piston push reaction from blocks.json");
    }

    PlacementStrategyFn chestStrategy = PlacementStrategyRegistry::getStrategy(chestDef.placementStrategy);
    if (chestStrategy == nullptr) {
        return fail("chest placement strategy should be registered");
    }

    PlacementContext chestPlacement;
    chestPlacement.blockId = BlockRegistry::requireIdByName("minecraft:chest");
    chestPlacement.playerYaw = 180.0f;
    const BlockStateId chestPlacedEast = chestStrategy(chestPlacement);
    if (BlockStateRegistry::getPropertyIndex(chestPlacedEast, PropIndices::FACING) != PropIndices::FACING_EAST) {
        return fail("horizontal_facing placement should apply the configured facing revert");
    }

    PlacementStrategyFn anvilStrategy = PlacementStrategyRegistry::getStrategy(anvilDef.placementStrategy);
    if (anvilStrategy == nullptr) {
        return fail("anvil placement strategy should be registered");
    }

    PlacementContext anvilPlacement;
    anvilPlacement.blockId = anvil;
    anvilPlacement.playerYaw = 270.0f;
    const BlockStateId anvilPlacedNorth = anvilStrategy(anvilPlacement);
    if (BlockStateRegistry::getPropertyIndex(anvilPlacedNorth, PropIndices::FACING) != PropIndices::FACING_NORTH) {
        return fail("anvil placement should derive facing from player yaw");
    }

    PlacementStrategyFn fenceStrategy = PlacementStrategyRegistry::getStrategy(oakFenceDef.placementStrategy);
    if (fenceStrategy == nullptr) {
        return fail("fence placement strategy should be registered");
    }

    PlacementContext fencePlacement;
    fencePlacement.blockId = oakFence;
    const BlockStateId fencePlaced = fenceStrategy(fencePlacement);
    if (fencePlaced != BlockStateRegistry::getDefaultState(oakFence)) {
        return fail("fence placement should start from the default disconnected state");
    }

    PlacementStrategyFn wallStrategy = PlacementStrategyRegistry::getStrategy(cobblestoneWallDef.placementStrategy);
    if (wallStrategy == nullptr) {
        return fail("wall placement strategy should be registered");
    }

    PlacementContext wallPlacement;
    wallPlacement.blockId = cobblestoneWall;
    const BlockStateId wallPlaced = wallStrategy(wallPlacement);
    if (wallPlaced != BlockStateRegistry::getDefaultState(cobblestoneWall)) {
        return fail("wall placement should start from the default disconnected state");
    }

    const uint16_t ageProperty = BlockStateRegistry::getPropertyNameIndex("age");
    if (ageProperty == BlockStateRegistry::INVALID_INDEX) {
        return fail("crop age property should be registered from blocks.json");
    }

    const BlockID farmland = BlockRegistry::findByName("farmland");
    if (farmland == RUNTIME_ID_NULL) {
        return fail("farmland should be registered for crop support");
    }
    const uint16_t moistureProperty = BlockStateRegistry::getPropertyNameIndex("moisture");
    if (moistureProperty == BlockStateRegistry::INVALID_INDEX) {
        return fail("farmland moisture property should be registered from blocks.json");
    }
    const BlockStateId farmlandDefault = BlockStateRegistry::getDefaultState(farmland);
    if (BlockStateRegistry::getPropertyIndex(farmlandDefault, moistureProperty) !=
        BlockStateRegistry::getPropertyValueIndex(moistureProperty, "0")) {
        return fail("farmland default moisture should be 0");
    }
    const BlockStateId farmlandMoist = BlockStateRegistry::withProperty(
        farmlandDefault, moistureProperty, BlockStateRegistry::getPropertyValueIndex(moistureProperty, "7"));
    if (farmlandMoist == farmlandDefault || BlockStateRegistry::getBlockId(farmlandMoist) != farmland) {
        return fail("farmland moisture 7 should resolve to a distinct farmland state");
    }
    if (!BlockRegistry::get(farmland).randomTick.enabled ||
        BlockRegistry::get(farmland).randomTick.behavior != "farmland_moisture") {
        return fail("farmland should register its random tick moisture behavior");
    }

    const BlockID wheat = BlockRegistry::findByName("wheat");
    if (wheat == RUNTIME_ID_NULL) {
        return fail("wheat crop block should be registered");
    }
    const BlockDef& wheatDef = BlockRegistry::get(wheat);
    if (wheatDef.renderShapeName != "cross" || wheatDef.renderShapeTag != MeshBuilderRegistry::getShapeTag("cross")) {
        return fail("wheat crop should use cross rendering");
    }
    if (wheatDef.supportRule != "farmland") {
        return fail("wheat crop should require farmland support");
    }
    if (wheatDef.stateTextureRules.size() != 1 || wheatDef.stateTextureRules.front().propertyName != "age" ||
        wheatDef.stateTextureRules.front().texturesByValue.size() != 8) {
        return fail("wheat crop should register age texture rules");
    }
    if (!wheatDef.randomTick.enabled || wheatDef.randomTick.behavior != "increment_property" ||
        wheatDef.randomTick.propertyName != "age") {
        return fail("wheat crop should register generic random tick age increment behavior");
    }
    const BlockStateId wheatDefault = BlockStateRegistry::getDefaultState(wheat);
    if (BlockStateRegistry::getPropertyIndex(wheatDefault, ageProperty) !=
        BlockStateRegistry::getPropertyValueIndex(ageProperty, "0")) {
        return fail("wheat crop default age should be 0");
    }
    const BlockStateId wheatMature = BlockStateRegistry::getState(
        wheat, std::vector<std::pair<uint16_t, uint16_t>>{
                   {ageProperty, BlockStateRegistry::getPropertyValueIndex(ageProperty, "7")}});
    if (wheatMature == wheatDefault || BlockStateRegistry::getBlockId(wheatMature) != wheat) {
        return fail("wheat crop age 7 should resolve to a distinct wheat state");
    }

    const BlockID carrots = BlockRegistry::findByName("carrots");
    const BlockID potatoes = BlockRegistry::findByName("potatoes");
    if (carrots == RUNTIME_ID_NULL || potatoes == RUNTIME_ID_NULL) {
        return fail("carrot and potato crop blocks should be registered");
    }
    const BlockDef& carrotsDef = BlockRegistry::get(carrots);
    const BlockDef& potatoesDef = BlockRegistry::get(potatoes);
    if (carrotsDef.stateTextureRules.size() != 1 || potatoesDef.stateTextureRules.size() != 1 ||
        carrotsDef.stateTextureRules.front().texturesByValue.size() != 4 ||
        potatoesDef.stateTextureRules.front().texturesByValue.size() != 4) {
        return fail("carrot and potato crops should register four age texture rules");
    }

    const BlockID vine = BlockRegistry::findByName("vine");
    if (vine == RUNTIME_ID_NULL) {
        return fail("vine should be registered for placement tests");
    }
    const BlockDef& vineDef = BlockRegistry::get(vine);
    if (vineDef.biomeTint != BiomeTintKind::Foliage) {
        return fail("vine should use foliage biome tint");
    }
    PlacementStrategyFn vineStrategy = PlacementStrategyRegistry::getStrategy(vineDef.placementStrategy);
    if (vineStrategy == nullptr) {
        return fail("vine placement strategy should be registered");
    }

    PlacementContext vinePlacement;
    vinePlacement.blockId = vine;
    vinePlacement.hitNormal = glm::ivec3(0, 0, -1);
    const BlockStateId northVinePlaced = vineStrategy(vinePlacement);
    if (BlockStateRegistry::getPropertyIndex(northVinePlaced, PropIndices::FACING) != PropIndices::FACING_NORTH) {
        return fail("wall face plane placement should derive facing from the clicked side");
    }
    vinePlacement.hitNormal = glm::ivec3(0, 1, 0);
    if (vineStrategy(vinePlacement) != NULL_BLOCK_STATE) {
        return fail("wall face plane placement should reject floor hits");
    }

    const BlockID pinkPetals = BlockRegistry::findByName("pink_petals");
    if (pinkPetals == RUNTIME_ID_NULL) {
        return fail("pink_petals should be registered for placement tests");
    }
    const BlockDef& pinkPetalsDef = BlockRegistry::get(pinkPetals);
    PlacementStrategyFn petalStrategy = PlacementStrategyRegistry::getStrategy(pinkPetalsDef.placementStrategy);
    if (petalStrategy == nullptr) {
        return fail("floor face plane placement strategy should be registered");
    }

    PlacementContext petalPlacement;
    petalPlacement.blockId = pinkPetals;
    petalPlacement.hitNormal = glm::ivec3(0, 1, 0);
    const BlockStateId floorPetalsPlaced = petalStrategy(petalPlacement);
    if (BlockStateRegistry::getPropertyIndex(floorPetalsPlaced, PropIndices::FACING) != PropIndices::FACING_FLOOR) {
        return fail("floor face plane placement should use the floor facing state");
    }
    petalPlacement.hitNormal = glm::ivec3(1, 0, 0);
    if (petalStrategy(petalPlacement) != NULL_BLOCK_STATE) {
        return fail("floor face plane placement should reject wall hits");
    }

    const BlockID wildflowers = BlockRegistry::findByName("wildflowers");
    const BlockID leafLitter = BlockRegistry::findByName("leaf_litter");
    const BlockID glowLichen = BlockRegistry::findByName("glow_lichen");
    if (wildflowers == RUNTIME_ID_NULL || leafLitter == RUNTIME_ID_NULL || glowLichen == RUNTIME_ID_NULL) {
        return fail("new face plane decoration blocks should be registered");
    }

    const BlockDef& wildflowersDef = BlockRegistry::get(wildflowers);
    const BlockDef& leafLitterDef = BlockRegistry::get(leafLitter);
    const BlockDef& glowLichenDef = BlockRegistry::get(glowLichen);
    if (wildflowersDef.renderShapeName != "face_plane" || leafLitterDef.renderShapeName != "face_plane" ||
        glowLichenDef.renderShapeName != "face_plane") {
        return fail("new decoration blocks should use the face plane render shape");
    }
    if (wildflowersDef.placementStrategy != "face_plane_floor" ||
        leafLitterDef.placementStrategy != "face_plane_floor" || glowLichenDef.placementStrategy != "face_plane_wall") {
        return fail("new face plane decorations should use the matching placement strategy");
    }
    if (!glowLichenDef.isLightSource || glowLichenDef.lightLevel != 7 || glowLichenDef.analyticLight.has_value()) {
        return fail("glow_lichen should emit propagated level 7 block light without an analytic light");
    }
    if (BlockStateRegistry::getPropertyIndex(BlockStateRegistry::getDefaultState(wildflowers), PropIndices::FACING) !=
            PropIndices::FACING_FLOOR ||
        BlockStateRegistry::getPropertyIndex(BlockStateRegistry::getDefaultState(leafLitter), PropIndices::FACING) !=
            PropIndices::FACING_FLOOR) {
        return fail("floor face plane decorations should default to facing=floor");
    }
    if (BlockStateRegistry::getState(glowLichen, PropIndices::FACING, PropIndices::FACING_NORTH) == NULL_BLOCK_STATE ||
        BlockStateRegistry::getState(glowLichen, PropIndices::FACING, PropIndices::FACING_SOUTH) == NULL_BLOCK_STATE ||
        BlockStateRegistry::getState(glowLichen, PropIndices::FACING, PropIndices::FACING_EAST) == NULL_BLOCK_STATE ||
        BlockStateRegistry::getState(glowLichen, PropIndices::FACING, PropIndices::FACING_WEST) == NULL_BLOCK_STATE) {
        return fail("glow_lichen should expose all horizontal wall facing states");
    }

    if (PropIndices::EXTENDED == PropIndices::INVALID || PropIndices::EXTENDED_TRUE == PropIndices::INVALID ||
        PropIndices::EXTENDED_FALSE == PropIndices::INVALID || PropIndices::POWER == PropIndices::INVALID ||
        PropIndices::POWER_0 == PropIndices::INVALID || PropIndices::POWER_15 == PropIndices::INVALID ||
        PropIndices::TYPE == PropIndices::INVALID || PropIndices::TYPE_NORMAL == PropIndices::INVALID ||
        PropIndices::TYPE_STICKY == PropIndices::INVALID || PropIndices::DELAY == PropIndices::INVALID ||
        PropIndices::DELAY_1 == PropIndices::INVALID || PropIndices::DELAY_2 == PropIndices::INVALID ||
        PropIndices::DELAY_3 == PropIndices::INVALID || PropIndices::DELAY_4 == PropIndices::INVALID ||
        PropIndices::MODE == PropIndices::INVALID || PropIndices::MODE_COMPARE == PropIndices::INVALID ||
        PropIndices::MODE_SUBTRACT == PropIndices::INVALID || PropIndices::FACING_CEILING == PropIndices::INVALID ||
        PropIndices::FACING_UP == PropIndices::INVALID || PropIndices::FACING_DOWN == PropIndices::INVALID) {
        return fail("redstone support properties should be registered from blocks.json");
    }

    const std::vector<std::pair<BlockID, const char*>> redstoneBlocks = {
        {BlockRegistry::requireIdByName("minecraft:redstone_wire"), "redstone_wire"},
        {BlockRegistry::requireIdByName("minecraft:redstone_torch"), "redstone_torch"},
        {BlockRegistry::requireIdByName("minecraft:redstone_lamp"), "redstone_lamp"},
        {BlockRegistry::requireIdByName("minecraft:lever"), "lever"},
        {BlockRegistry::requireIdByName("minecraft:stone_button"), "stone_button"},
        {BlockRegistry::requireIdByName("minecraft:oak_button"), "oak_button"},
        {BlockRegistry::requireIdByName("minecraft:stone_pressure_plate"), "stone_pressure_plate"},
        {BlockRegistry::requireIdByName("minecraft:oak_pressure_plate"), "oak_pressure_plate"},
        {BlockRegistry::requireIdByName("minecraft:repeater"), "repeater"},
        {BlockRegistry::requireIdByName("minecraft:comparator"), "comparator"},
        {BlockRegistry::requireIdByName("minecraft:piston"), "piston"},
        {BlockRegistry::requireIdByName("minecraft:sticky_piston"), "sticky_piston"},
        {BlockRegistry::requireIdByName("minecraft:piston_head"), "piston_head"},
        {BlockRegistry::requireIdByName("minecraft:observer"), "observer"},
        {BlockRegistry::requireIdByName("minecraft:dispenser"), "dispenser"},
        {BlockRegistry::requireIdByName("minecraft:dropper"), "dropper"},
        {BlockRegistry::requireIdByName("minecraft:hopper"), "hopper"},
        {BlockRegistry::requireIdByName("minecraft:note_block"), "note_block"},
        {BlockRegistry::requireIdByName("minecraft:target"), "target"},
    };
    for (const auto& [blockId, name] : redstoneBlocks) {
        if (blockId == RUNTIME_ID_NULL) {
            std::cerr << "[block_state_registry_test] missing redstone block: " << name << '\n';
            return fail("redstone blocks should be registered from blocks.json");
        }
    }

    const BlockStateId noteBlockDefault =
        BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:note_block"));
    if (BlockStateRegistry::getPropertyIndex(noteBlockDefault, PropIndices::POWERED) != PropIndices::POWERED_FALSE) {
        return fail("note_block default state should be powered=false");
    }
    const BlockStateId noteBlockPowered =
        BlockStateRegistry::withProperty(noteBlockDefault, PropIndices::POWERED, PropIndices::POWERED_TRUE);
    if (noteBlockPowered == noteBlockDefault ||
        BlockStateRegistry::getBlockId(noteBlockPowered) != BlockRegistry::requireIdByName("minecraft:note_block") ||
        BlockStateRegistry::getPropertyIndex(noteBlockPowered, PropIndices::POWERED) != PropIndices::POWERED_TRUE) {
        return fail("note_block should expose a powered=true block state");
    }
    const BlockStateId dispenserDefault =
        BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:dispenser"));
    if (BlockStateRegistry::getPropertyIndex(dispenserDefault, PropIndices::POWERED) != PropIndices::POWERED_FALSE) {
        return fail("dispenser default state should be powered=false");
    }
    const BlockStateId dispenserPowered =
        BlockStateRegistry::withProperty(dispenserDefault, PropIndices::POWERED, PropIndices::POWERED_TRUE);
    if (dispenserPowered == dispenserDefault ||
        BlockStateRegistry::getBlockId(dispenserPowered) != BlockRegistry::requireIdByName("minecraft:dispenser") ||
        BlockStateRegistry::getPropertyIndex(dispenserPowered, PropIndices::POWERED) != PropIndices::POWERED_TRUE) {
        return fail("dispenser should expose a powered=true block state");
    }
    if (BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:dispenser")).containerUi !=
        "minecraft:dispenser") {
        return fail("dispenser should bind to its container UI");
    }
    const BlockStateId dropperDefault =
        BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:dropper"));
    if (BlockStateRegistry::getPropertyIndex(dropperDefault, PropIndices::POWERED) != PropIndices::POWERED_FALSE) {
        return fail("dropper default state should be powered=false");
    }
    const BlockStateId dropperPowered =
        BlockStateRegistry::withProperty(dropperDefault, PropIndices::POWERED, PropIndices::POWERED_TRUE);
    if (dropperPowered == dropperDefault ||
        BlockStateRegistry::getBlockId(dropperPowered) != BlockRegistry::requireIdByName("minecraft:dropper") ||
        BlockStateRegistry::getPropertyIndex(dropperPowered, PropIndices::POWERED) != PropIndices::POWERED_TRUE) {
        return fail("dropper should expose a powered=true block state");
    }
    if (BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:dropper")).containerUi != "minecraft:dropper") {
        return fail("dropper should bind to its container UI");
    }

    const auto modelVariantMatches = [](const BlockStateId state, const char* expectedModel,
                                        const uint16_t expectedRotY, const uint16_t expectedRotX) {
        const ModelVariant* variant = BlockStateRegistry::getModelVariant(state);
        return variant != nullptr && variant->model != nullptr && variant->model->name == expectedModel &&
               variant->transform.rotY == expectedRotY && variant->transform.rotX == expectedRotX;
    };
    const auto modelFaceTextureMatches = [](const BlockModel* model, const size_t faceIndex,
                                            const char* expectedTexture) {
        if (model == nullptr || model->elements.empty() || faceIndex >= model->elements.front().faces.size()) {
            return false;
        }
        const std::unique_ptr<ModelFace>& face = model->elements.front().faces[faceIndex];
        if (face == nullptr || face->textureVar.empty() || face->textureVar.front() != '#') {
            return false;
        }
        const auto textureIt = model->textures.find(face->textureVar.substr(1));
        return textureIt != model->textures.end() && textureIt->second == expectedTexture;
    };
    const auto modelFaceRotationMatches = [](const BlockModel* model, const size_t faceIndex,
                                             const uint16_t expectedRotation) {
        if (model == nullptr || model->elements.empty() || faceIndex >= model->elements.front().faces.size()) {
            return false;
        }
        const std::unique_ptr<ModelFace>& face = model->elements.front().faces[faceIndex];
        return face != nullptr && face->uvRotation == expectedRotation;
    };

    const std::vector<BlockID> redstoneModelBlocks = {
        BlockRegistry::requireIdByName("minecraft:lever"),
        BlockRegistry::requireIdByName("minecraft:stone_button"),
        BlockRegistry::requireIdByName("minecraft:oak_button"),
        BlockRegistry::requireIdByName("minecraft:stone_pressure_plate"),
        BlockRegistry::requireIdByName("minecraft:oak_pressure_plate"),
        BlockRegistry::requireIdByName("minecraft:repeater"),
        BlockRegistry::requireIdByName("minecraft:comparator"),
        BlockRegistry::requireIdByName("minecraft:piston"),
        BlockRegistry::requireIdByName("minecraft:sticky_piston"),
        BlockRegistry::requireIdByName("minecraft:piston_head"),
        BlockRegistry::requireIdByName("minecraft:observer"),
        BlockRegistry::requireIdByName("minecraft:dispenser"),
        BlockRegistry::requireIdByName("minecraft:dropper"),
        BlockRegistry::requireIdByName("minecraft:hopper"),
        BlockRegistry::requireIdByName("minecraft:oak_door"),
        BlockRegistry::requireIdByName("minecraft:oak_trapdoor"),
        BlockRegistry::requireIdByName("minecraft:oak_fence_gate"),
    };
    for (const BlockID blockId : redstoneModelBlocks) {
        for (const BlockStateId state : BlockStateRegistry::getStatesForBlock(blockId)) {
            const ModelVariant* variant = BlockStateRegistry::getModelVariant(state);
            if (variant == nullptr || variant->model == nullptr) {
                std::cerr << "[block_state_registry_test] missing redstone model variant for "
                          << BlockStateRegistry::stateToString(state) << '\n';
                return fail("redstone model blocks should expose model variants for every state");
            }
        }
    }
    constexpr size_t modelFaceUp = 0;
    constexpr size_t modelFaceDown = 1;
    constexpr size_t modelFaceSouth = 2;
    constexpr size_t modelFaceNorth = 3;
    constexpr size_t modelFaceWest = 4;
    constexpr size_t modelFaceEast = 5;
    if (!modelFaceTextureMatches(BlockModelRegistry::get("block/repeater_1tick"), modelFaceDown, "smooth_stone") ||
        !modelFaceTextureMatches(BlockModelRegistry::get("block/repeater_1tick"), modelFaceSouth, "smooth_stone") ||
        !modelFaceTextureMatches(BlockModelRegistry::get("block/comparator"), modelFaceDown, "smooth_stone") ||
        !modelFaceTextureMatches(BlockModelRegistry::get("block/comparator"), modelFaceSouth, "smooth_stone")) {
        return fail("repeater and comparator models should use smooth_stone for their slab base");
    }
    const BlockModel* repeater4TickOnModel = BlockModelRegistry::get("block/repeater_4tick_on");
    const BlockModel* comparatorSubtractPoweredModel = BlockModelRegistry::get("block/comparator_subtract_powered");
    if (repeater4TickOnModel == nullptr || repeater4TickOnModel->elements.size() <= 3 ||
        comparatorSubtractPoweredModel == nullptr || comparatorSubtractPoweredModel->elements.size() <= 4) {
        return fail("powered repeater and subtract comparator models should include lit torch face elements");
    }
    const auto pistonBodyFacesMatch = [&](const char* modelName, const char* frontTexture) {
        const BlockModel* model = BlockModelRegistry::get(modelName);
        return modelFaceTextureMatches(model, modelFaceUp, "piston_side") &&
               modelFaceTextureMatches(model, modelFaceDown, "piston_side") &&
               modelFaceTextureMatches(model, modelFaceSouth, frontTexture) &&
               modelFaceTextureMatches(model, modelFaceNorth, "piston_bottom") &&
               modelFaceTextureMatches(model, modelFaceWest, "piston_side") &&
               modelFaceTextureMatches(model, modelFaceEast, "piston_side") &&
               modelFaceRotationMatches(model, modelFaceUp, 180) && modelFaceRotationMatches(model, modelFaceDown, 0) &&
               modelFaceRotationMatches(model, modelFaceWest, 90) &&
               modelFaceRotationMatches(model, modelFaceEast, 270);
    };
    if (!pistonBodyFacesMatch("block/piston", "piston_top") ||
        !pistonBodyFacesMatch("block/sticky_piston", "piston_top_sticky") ||
        !pistonBodyFacesMatch("block/piston_extended", "piston_inner") ||
        !pistonBodyFacesMatch("block/sticky_piston_extended", "piston_inner")) {
        return fail("piston body models should orient side textures toward the local front face");
    }

    const auto declaresPoweredOpenMirror = [](const BlockDef& def) {
        return def.respondsToRedstone && def.redstoneControlledProperty == "powered" &&
               def.redstoneControlledMirrorProperties.size() == 1 &&
               def.redstoneControlledMirrorProperties.front() == "open";
    };
    const BlockDef& oakDoorDef = BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:oak_door"));
    if (oakDoorDef.renderShapeName != "model" || oakDoorDef.renderLayer != BlockRenderLayer::Cutout ||
        oakDoorDef.placementStrategy != "door" || oakDoorDef.isSolid || oakDoorDef.isRedstoneConductor ||
        !declaresPoweredOpenMirror(oakDoorDef)) {
        return fail("oak_door should declare model rendering, door placement, and powered open redstone control");
    }
    if (!DoorBlockLogic::isDoorBlock(BlockRegistry::requireIdByName("minecraft:oak_door"))) {
        return fail("oak_door should be recognized by door block logic through placementStrategy");
    }
    const BlockStateId oakDoorDefault =
        BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:oak_door"));
    if (!DoorBlockLogic::isLowerState(oakDoorDefault) ||
        BlockStateRegistry::getPropertyIndex(oakDoorDefault, PropIndices::FACING) != PropIndices::FACING_EAST ||
        BlockStateRegistry::getPropertyIndex(oakDoorDefault, PropIndices::HINGE) != PropIndices::HINGE_LEFT ||
        BlockStateRegistry::getPropertyIndex(oakDoorDefault, PropIndices::OPEN) != PropIndices::OPEN_FALSE ||
        BlockStateRegistry::getPropertyIndex(oakDoorDefault, PropIndices::POWERED) != PropIndices::POWERED_FALSE) {
        return fail("oak_door default state should be an east-facing closed unpowered lower-left half");
    }
    const BlockStateId oakDoorUpper =
        DoorBlockLogic::makeDoorState(BlockRegistry::requireIdByName("minecraft:oak_door"), PropIndices::FACING_EAST,
                                      PropIndices::HALF_UPPER, PropIndices::HINGE_LEFT, false, false);
    if (!DoorBlockLogic::isUpperState(oakDoorUpper) ||
        !DoorBlockLogic::isMatchingOtherHalf(oakDoorDefault, oakDoorUpper)) {
        return fail("door helper should construct a matching upper half for oak_door");
    }
    const BlockStateId oakDoorPowered =
        BlockStateRegistry::withProperty(oakDoorDefault, PropIndices::POWERED, PropIndices::POWERED_TRUE);
    if (!modelVariantMatches(oakDoorPowered, "block/oak_door_bottom_left", 0, 0)) {
        return fail("oak_door powered=false/true lower-left states should share the closed model variant");
    }
    const BlockStateId oakDoorOpenPowered = BlockStateRegistry::getState(
        BlockRegistry::requireIdByName("minecraft:oak_door"), {{PropIndices::FACING, PropIndices::FACING_EAST},
                                                               {PropIndices::HALF, PropIndices::HALF_LOWER},
                                                               {PropIndices::HINGE, PropIndices::HINGE_LEFT},
                                                               {PropIndices::OPEN, PropIndices::OPEN_TRUE},
                                                               {PropIndices::POWERED, PropIndices::POWERED_TRUE}});
    if (!modelVariantMatches(oakDoorOpenPowered, "block/oak_door_bottom_left_open", 90, 0)) {
        return fail("oak_door open powered state should resolve through a partial model variant key");
    }
    const BlockStateId oakDoorOpenRight = BlockStateRegistry::getState(
        BlockRegistry::requireIdByName("minecraft:oak_door"), {{PropIndices::FACING, PropIndices::FACING_EAST},
                                                               {PropIndices::HALF, PropIndices::HALF_LOWER},
                                                               {PropIndices::HINGE, PropIndices::HINGE_RIGHT},
                                                               {PropIndices::OPEN, PropIndices::OPEN_TRUE},
                                                               {PropIndices::POWERED, PropIndices::POWERED_FALSE}});
    if (!modelVariantMatches(oakDoorOpenRight, "block/oak_door_bottom_right_open", 270, 0)) {
        return fail("oak_door right-hinge open state should rotate to the opposite side of the block");
    }

    const BlockDef& oakTrapdoorDef = BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:oak_trapdoor"));
    if (oakTrapdoorDef.renderShapeName != "model" || oakTrapdoorDef.renderLayer != BlockRenderLayer::Cutout ||
        oakTrapdoorDef.placementStrategy != "trapdoor" || oakTrapdoorDef.isSolid ||
        oakTrapdoorDef.isRedstoneConductor || !declaresPoweredOpenMirror(oakTrapdoorDef)) {
        return fail(
            "oak_trapdoor should declare model rendering, trapdoor placement, and powered open redstone control");
    }
    const BlockStateId oakTrapdoorClosedPowered = BlockStateRegistry::getState(
        BlockRegistry::requireIdByName("minecraft:oak_trapdoor"), {{PropIndices::FACING, PropIndices::FACING_WEST},
                                                                   {PropIndices::HALF, PropIndices::HALF_BOTTOM},
                                                                   {PropIndices::OPEN, PropIndices::OPEN_FALSE},
                                                                   {PropIndices::POWERED, PropIndices::POWERED_TRUE}});
    if (!modelVariantMatches(oakTrapdoorClosedPowered, "block/oak_trapdoor_bottom", 0, 0)) {
        return fail("oak_trapdoor closed powered state should resolve through a partial half/open model key");
    }
    const BlockStateId oakTrapdoorOpenEast = BlockStateRegistry::getState(
        BlockRegistry::requireIdByName("minecraft:oak_trapdoor"), {{PropIndices::FACING, PropIndices::FACING_EAST},
                                                                   {PropIndices::HALF, PropIndices::HALF_TOP},
                                                                   {PropIndices::OPEN, PropIndices::OPEN_TRUE},
                                                                   {PropIndices::POWERED, PropIndices::POWERED_FALSE}});
    if (!modelVariantMatches(oakTrapdoorOpenEast, "block/oak_trapdoor_open", 270, 0)) {
        return fail("oak_trapdoor open east state should resolve through a partial facing/open model key");
    }

    const BlockDef& oakFenceGateDef = BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:oak_fence_gate"));
    if (oakFenceGateDef.renderShapeName != "model" || oakFenceGateDef.placementStrategy != "horizontal_facing" ||
        oakFenceGateDef.isSolid || oakFenceGateDef.isRedstoneConductor || !declaresPoweredOpenMirror(oakFenceGateDef)) {
        return fail(
            "oak_fence_gate should declare model rendering, horizontal placement, and powered open redstone control");
    }
    const BlockStateId oakFenceGateEastPowered =
        BlockStateRegistry::getState(BlockRegistry::requireIdByName("minecraft:oak_fence_gate"),
                                     {{PropIndices::FACING, PropIndices::FACING_EAST},
                                      {PropIndices::OPEN, PropIndices::OPEN_FALSE},
                                      {PropIndices::POWERED, PropIndices::POWERED_TRUE}});
    if (!modelVariantMatches(oakFenceGateEastPowered, "block/oak_fence_gate", 90, 0)) {
        return fail("oak_fence_gate powered states should resolve through partial facing/open model variants");
    }
    const BlockStateId oakFenceGateWestOpen =
        BlockStateRegistry::getState(BlockRegistry::requireIdByName("minecraft:oak_fence_gate"),
                                     {{PropIndices::FACING, PropIndices::FACING_WEST},
                                      {PropIndices::OPEN, PropIndices::OPEN_TRUE},
                                      {PropIndices::POWERED, PropIndices::POWERED_FALSE}});
    if (!modelVariantMatches(oakFenceGateWestOpen, "block/oak_fence_gate_open", 270, 0)) {
        return fail("oak_fence_gate open west state should resolve to the rotated open gate model");
    }

    const BlockDef& redstoneWireDef = BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:redstone_wire"));
    if (redstoneWireDef.renderShapeName != "redstone_wire" ||
        redstoneWireDef.renderShapeTag != MeshBuilderRegistry::REDSTONE_WIRE_TAG ||
        redstoneWireDef.placementStrategy != "redstone_wire_face" || redstoneWireDef.supportRule != "attached_face" ||
        redstoneWireDef.redstoneWireChannel != "minecraft:red" || redstoneWireDef.redstoneWireChannelId == 0 ||
        redstoneWireDef.redstoneWireTint != 0) {
        return fail("redstone_wire should use the custom wire render shape and multi-face support path");
    }
    const BlockDef& blueRedstoneWireDef =
        BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:blue_redstone_wire"));
    if (blueRedstoneWireDef.renderShapeName != "redstone_wire" ||
        blueRedstoneWireDef.renderShapeTag != MeshBuilderRegistry::REDSTONE_WIRE_TAG ||
        blueRedstoneWireDef.placementStrategy != "redstone_wire_face" ||
        blueRedstoneWireDef.redstoneBehavior != "wire" || blueRedstoneWireDef.redstoneWireChannel != "minecraft:blue" ||
        blueRedstoneWireDef.redstoneWireChannelId == 0 ||
        blueRedstoneWireDef.redstoneWireChannelId == redstoneWireDef.redstoneWireChannelId ||
        blueRedstoneWireDef.redstoneWireTint != 1) {
        return fail("blue_redstone_wire should declare its wire channel and tint metadata");
    }
    const BlockDef& wireContainerDef = BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:wire_container"));
    if (wireContainerDef.renderShapeName != "wire_container" ||
        wireContainerDef.renderShapeTag != MeshBuilderRegistry::WIRE_CONTAINER_TAG ||
        wireContainerDef.redstoneBehavior != "wire_container" || !wireContainerDef.isWireContainer ||
        wireContainerDef.isRedstoneConductor || wireContainerDef.isSolid || !wireContainerDef.isTransparent ||
        !wireContainerDef.allowsFluidCoexistence || wireContainerDef.pistonPushReaction != "block") {
        return fail("wire_container should register as a non-conductive redstone wire part container");
    }
    const BlockStateId redstoneWireDefault =
        BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire"));
    const std::size_t expectedWireStateCount = 6u * 16u * 2u * 2u * 2u * 2u;
    if (BlockStateRegistry::getStatesForBlock(BlockRegistry::requireIdByName("minecraft:redstone_wire")).size() !=
            expectedWireStateCount ||
        BlockStateRegistry::getStatesForBlock(BlockRegistry::requireIdByName("minecraft:blue_redstone_wire")).size() !=
            expectedWireStateCount) {
        return fail("redstone_wire states should only expand none/side directional connections");
    }
    if (BlockStateRegistry::getPropertyIndex(redstoneWireDefault, PropIndices::FACING) != PropIndices::FACING_FLOOR ||
        BlockStateRegistry::getPropertyIndex(redstoneWireDefault, PropIndices::POWER) != PropIndices::POWER_0 ||
        BlockStateRegistry::getPropertyIndex(redstoneWireDefault, PropIndices::NORTH) != PropIndices::NORTH_NONE ||
        BlockStateRegistry::getPropertyIndex(redstoneWireDefault, PropIndices::SOUTH) != PropIndices::SOUTH_NONE ||
        BlockStateRegistry::getPropertyIndex(redstoneWireDefault, PropIndices::EAST) != PropIndices::EAST_NONE ||
        BlockStateRegistry::getPropertyIndex(redstoneWireDefault, PropIndices::WEST) != PropIndices::WEST_NONE) {
        return fail("redstone_wire default state should be floor power 0 with no visual connections");
    }
    const BlockStateId redstoneWirePower15 =
        BlockStateRegistry::withProperty(redstoneWireDefault, PropIndices::POWER, PropIndices::POWER_15);
    if (redstoneWirePower15 == redstoneWireDefault ||
        BlockStateRegistry::getPropertyIndex(redstoneWirePower15, PropIndices::POWER) != PropIndices::POWER_15) {
        return fail("redstone_wire should expose power 15 state");
    }
    const BlockStateId redstoneWireEast =
        BlockStateRegistry::withProperty(redstoneWireDefault, PropIndices::EAST, PropIndices::EAST_SIDE);
    if (redstoneWireEast == redstoneWireDefault ||
        BlockStateRegistry::getPropertyIndex(redstoneWireEast, PropIndices::EAST) != PropIndices::EAST_SIDE) {
        return fail("redstone_wire should expose directional visual connection states");
    }
    const BlockStateId redstoneWireCeiling =
        BlockStateRegistry::withProperty(redstoneWireDefault, PropIndices::FACING, PropIndices::FACING_CEILING);
    const BlockStateId redstoneWireNorthWall =
        BlockStateRegistry::withProperty(redstoneWireDefault, PropIndices::FACING, PropIndices::FACING_NORTH);
    if (redstoneWireCeiling == redstoneWireDefault || redstoneWireNorthWall == redstoneWireDefault ||
        BlockStateRegistry::getPropertyIndex(redstoneWireCeiling, PropIndices::FACING) != PropIndices::FACING_CEILING ||
        BlockStateRegistry::getPropertyIndex(redstoneWireNorthWall, PropIndices::FACING) != PropIndices::FACING_NORTH) {
        return fail("redstone_wire should expose ceiling and wall facing states");
    }
    PlacementStrategyFn redstoneWireStrategy =
        PlacementStrategyRegistry::getStrategy(redstoneWireDef.placementStrategy);
    if (redstoneWireStrategy == nullptr) {
        return fail("redstone_wire face placement strategy should be registered");
    }
    PlacementContext wirePlacement;
    wirePlacement.blockId = BlockRegistry::requireIdByName("minecraft:redstone_wire");
    wirePlacement.hitNormal = glm::ivec3(0, 1, 0);
    if (BlockStateRegistry::getPropertyIndex(redstoneWireStrategy(wirePlacement), PropIndices::FACING) !=
        PropIndices::FACING_FLOOR) {
        return fail("redstone_wire placement should use floor facing for top-face hits");
    }
    wirePlacement.hitNormal = glm::ivec3(0, -1, 0);
    if (BlockStateRegistry::getPropertyIndex(redstoneWireStrategy(wirePlacement), PropIndices::FACING) !=
        PropIndices::FACING_CEILING) {
        return fail("redstone_wire placement should use ceiling facing for bottom-face hits");
    }
    wirePlacement.hitNormal = glm::ivec3(0, 0, -1);
    if (BlockStateRegistry::getPropertyIndex(redstoneWireStrategy(wirePlacement), PropIndices::FACING) !=
        PropIndices::FACING_NORTH) {
        return fail("redstone_wire placement should derive wall facing from side hits");
    }

    const BlockDef& redstoneTorchDef = BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:redstone_torch"));
    if (redstoneTorchDef.renderShapeName != "torch" || redstoneTorchDef.placementStrategy != "attach_wall" ||
        redstoneTorchDef.supportRule != "attached_face") {
        return fail("redstone_torch should use torch rendering and attach-wall placement");
    }
    const BlockStateId redstoneTorchDefault =
        BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_torch"));
    if (BlockStateRegistry::getPropertyIndex(redstoneTorchDefault, PropIndices::FACING) != PropIndices::FACING_FLOOR ||
        BlockStateRegistry::getPropertyIndex(redstoneTorchDefault, PropIndices::LIT) != PropIndices::LIT_TRUE) {
        return fail("redstone_torch default state should be floor lit true");
    }

    const BlockStateId redstoneLampLit = BlockStateRegistry::getState(
        BlockRegistry::requireIdByName("minecraft:redstone_lamp"), PropIndices::LIT, PropIndices::LIT_TRUE);
    const BlockDef& redstoneLampDef = BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:redstone_lamp"));
    if (!redstoneLampDef.respondsToRedstone || redstoneLampDef.redstoneControlledProperty != "lit") {
        return fail("redstone_lamp should declare lit as its redstone controlled property");
    }
    if (redstoneLampLit ==
            BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_lamp")) ||
        BlockStateRegistry::getPropertyIndex(redstoneLampLit, PropIndices::LIT) != PropIndices::LIT_TRUE) {
        return fail("redstone_lamp should expose lit and unlit display states");
    }

    const BlockDef& targetDef = BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:target"));
    const BlockStateId targetDefault =
        BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:target"));
    const BlockStateId targetPower15 = BlockStateRegistry::getState(BlockRegistry::requireIdByName("minecraft:target"),
                                                                    PropIndices::POWER, PropIndices::POWER_15);
    if (targetDef.redstoneBehavior != "target" || !targetDef.isRedstonePowerSource ||
        BlockStateRegistry::getPropertyIndex(targetDefault, PropIndices::POWER) != PropIndices::POWER_0 ||
        targetPower15 == targetDefault ||
        BlockStateRegistry::getPropertyIndex(targetPower15, PropIndices::POWER) != PropIndices::POWER_15) {
        return fail("target should expose variable redstone power states");
    }

    const BlockDef& leverDef = BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:lever"));
    if (leverDef.renderShapeName != "model" || leverDef.placementStrategy != "attach_wall" ||
        leverDef.supportRule != "attached_face") {
        return fail("lever should use model rendering and attach-wall placement");
    }
    PlacementStrategyFn leverStrategy = PlacementStrategyRegistry::getStrategy(leverDef.placementStrategy);
    if (leverStrategy == nullptr) {
        return fail("lever placement strategy should be registered");
    }
    PlacementContext leverWallPlacement;
    leverWallPlacement.blockId = BlockRegistry::requireIdByName("minecraft:lever");
    leverWallPlacement.hitNormal = {0, 0, -1};
    const BlockStateId leverPlacedNorth = leverStrategy(leverWallPlacement);
    if (BlockStateRegistry::getPropertyIndex(leverPlacedNorth, PropIndices::FACING) != PropIndices::FACING_NORTH) {
        return fail("lever wall placement should derive outward facing from hit normal");
    }
    PlacementContext leverFloorPlacement;
    leverFloorPlacement.blockId = BlockRegistry::requireIdByName("minecraft:lever");
    leverFloorPlacement.hitNormal = {0, 1, 0};
    const BlockStateId leverPlacedFloor = leverStrategy(leverFloorPlacement);
    if (BlockStateRegistry::getPropertyIndex(leverPlacedFloor, PropIndices::FACING) != PropIndices::FACING_FLOOR) {
        return fail("lever top-face placement should use the floor-facing state");
    }

    const BlockStateId leverNorthUnpowered = BlockStateRegistry::getState(
        BlockRegistry::requireIdByName("minecraft:lever"),
        std::vector<std::pair<uint16_t, uint16_t>>{{PropIndices::FACING, PropIndices::FACING_NORTH},
                                                   {PropIndices::POWERED, PropIndices::POWERED_FALSE}});
    if (!modelVariantMatches(leverNorthUnpowered, "block/lever_wall", 180, 0)) {
        return fail("lever north unpowered state should face out from the supporting wall");
    }
    const BlockStateId leverSouthPowered = BlockStateRegistry::getState(
        BlockRegistry::requireIdByName("minecraft:lever"),
        std::vector<std::pair<uint16_t, uint16_t>>{{PropIndices::FACING, PropIndices::FACING_SOUTH},
                                                   {PropIndices::POWERED, PropIndices::POWERED_TRUE}});
    if (!modelVariantMatches(leverSouthPowered, "block/lever_wall_powered", 0, 0)) {
        return fail("lever south powered state should use the unrotated powered wall model");
    }
    const BlockStateId leverEastPowered = BlockStateRegistry::getState(
        BlockRegistry::requireIdByName("minecraft:lever"),
        std::vector<std::pair<uint16_t, uint16_t>>{{PropIndices::FACING, PropIndices::FACING_EAST},
                                                   {PropIndices::POWERED, PropIndices::POWERED_TRUE}});
    if (!modelVariantMatches(leverEastPowered, "block/lever_wall_powered", 270, 0)) {
        return fail("lever east powered state should rotate the powered wall model outward");
    }
    const BlockStateId leverWestPowered = BlockStateRegistry::getState(
        BlockRegistry::requireIdByName("minecraft:lever"),
        std::vector<std::pair<uint16_t, uint16_t>>{{PropIndices::FACING, PropIndices::FACING_WEST},
                                                   {PropIndices::POWERED, PropIndices::POWERED_TRUE}});
    if (!modelVariantMatches(leverWestPowered, "block/lever_wall_powered", 90, 0)) {
        return fail("lever west powered state should resolve to the rotated powered wall model");
    }

    const BlockDef& stoneButtonDef = BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:stone_button"));
    if (stoneButtonDef.renderShapeName != "model" || stoneButtonDef.placementStrategy != "attach_wall" ||
        stoneButtonDef.supportRule != "attached_face") {
        return fail("stone_button should use model rendering and attach-wall placement");
    }
    PlacementStrategyFn stoneButtonStrategy = PlacementStrategyRegistry::getStrategy(stoneButtonDef.placementStrategy);
    if (stoneButtonStrategy == nullptr) {
        return fail("stone_button placement strategy should be registered");
    }
    PlacementContext stoneButtonWallPlacement;
    stoneButtonWallPlacement.blockId = BlockRegistry::requireIdByName("minecraft:stone_button");
    stoneButtonWallPlacement.hitNormal = {0, 0, -1};
    const BlockStateId stoneButtonPlacedNorth = stoneButtonStrategy(stoneButtonWallPlacement);
    if (BlockStateRegistry::getPropertyIndex(stoneButtonPlacedNorth, PropIndices::FACING) !=
            PropIndices::FACING_NORTH ||
        !modelVariantMatches(stoneButtonPlacedNorth, "block/stone_button_wall", 180, 0)) {
        return fail("stone_button wall placement should face and render outward from the supporting wall");
    }

    const BlockStateId stoneButtonEastPressed = BlockStateRegistry::getState(
        BlockRegistry::requireIdByName("minecraft:stone_button"),
        std::vector<std::pair<uint16_t, uint16_t>>{{PropIndices::FACING, PropIndices::FACING_EAST},
                                                   {PropIndices::POWERED, PropIndices::POWERED_TRUE}});
    if (!modelVariantMatches(stoneButtonEastPressed, "block/stone_button_wall_pressed", 270, 0)) {
        return fail("stone_button east powered state should resolve to the rotated pressed wall model");
    }

    const BlockStateId oakButtonWestPressed = BlockStateRegistry::getState(
        BlockRegistry::requireIdByName("minecraft:oak_button"),
        std::vector<std::pair<uint16_t, uint16_t>>{{PropIndices::FACING, PropIndices::FACING_WEST},
                                                   {PropIndices::POWERED, PropIndices::POWERED_TRUE}});
    if (!modelVariantMatches(oakButtonWestPressed, "block/oak_button_wall_pressed", 90, 0)) {
        return fail("oak_button west powered state should resolve to the rotated pressed wall model");
    }

    const BlockStateId oakPressurePlatePressed =
        BlockStateRegistry::getState(BlockRegistry::requireIdByName("minecraft:oak_pressure_plate"),
                                     PropIndices::POWERED, PropIndices::POWERED_TRUE);
    if (!modelVariantMatches(oakPressurePlatePressed, "block/oak_pressure_plate_pressed", 0, 0)) {
        return fail("oak_pressure_plate powered state should resolve to the pressed model");
    }

    const BlockStateId repeaterDefault =
        BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:repeater"));
    if (BlockStateRegistry::getPropertyIndex(repeaterDefault, PropIndices::DELAY) != PropIndices::DELAY_1) {
        return fail("repeater default state should use delay 1");
    }
    if (BlockStateRegistry::getPropertyIndex(repeaterDefault, PropIndices::LOCKED) != PropIndices::LOCKED_FALSE) {
        return fail("repeater default state should not be locked");
    }
    const BlockStateId repeaterEastPowered = BlockStateRegistry::getState(
        BlockRegistry::requireIdByName("minecraft:repeater"),
        std::vector<std::pair<uint16_t, uint16_t>>{{PropIndices::FACING, PropIndices::FACING_EAST},
                                                   {PropIndices::POWERED, PropIndices::POWERED_TRUE}});
    if (!modelVariantMatches(repeaterEastPowered, "block/repeater_1tick_on", 270, 0)) {
        return fail("repeater east powered default-delay state should resolve to the rotated 1-tick powered model");
    }
    const BlockStateId repeaterEastDelay4Powered = BlockStateRegistry::getState(
        BlockRegistry::requireIdByName("minecraft:repeater"),
        std::vector<std::pair<uint16_t, uint16_t>>{{PropIndices::FACING, PropIndices::FACING_EAST},
                                                   {PropIndices::POWERED, PropIndices::POWERED_TRUE},
                                                   {PropIndices::DELAY, PropIndices::DELAY_4}});
    if (!modelVariantMatches(repeaterEastDelay4Powered, "block/repeater_4tick_on", 270, 0)) {
        return fail("repeater delay 4 powered state should resolve to the rotated 4-tick powered model");
    }
    const BlockStateId lockedRepeaterEastDelay4Powered = BlockStateRegistry::getState(
        BlockRegistry::requireIdByName("minecraft:repeater"),
        std::vector<std::pair<uint16_t, uint16_t>>{{PropIndices::FACING, PropIndices::FACING_EAST},
                                                   {PropIndices::POWERED, PropIndices::POWERED_TRUE},
                                                   {PropIndices::DELAY, PropIndices::DELAY_4},
                                                   {PropIndices::LOCKED, PropIndices::LOCKED_TRUE}});
    if (!modelVariantMatches(lockedRepeaterEastDelay4Powered, "block/repeater_4tick_on_locked", 270, 0)) {
        return fail("locked repeater should resolve to the powered locked delay model variant");
    }

    const BlockStateId comparatorDefault =
        BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:comparator"));
    if (BlockStateRegistry::getPropertyIndex(comparatorDefault, PropIndices::MODE) != PropIndices::MODE_COMPARE) {
        return fail("comparator default state should use compare mode");
    }
    const BlockStateId comparatorWestPowered = BlockStateRegistry::getState(
        BlockRegistry::requireIdByName("minecraft:comparator"),
        std::vector<std::pair<uint16_t, uint16_t>>{{PropIndices::FACING, PropIndices::FACING_WEST},
                                                   {PropIndices::POWERED, PropIndices::POWERED_TRUE}});
    if (!modelVariantMatches(comparatorWestPowered, "block/comparator_powered", 90, 0)) {
        return fail("comparator west powered state should resolve to the rotated powered model");
    }
    const BlockStateId comparatorWestSubtractPowered = BlockStateRegistry::getState(
        BlockRegistry::requireIdByName("minecraft:comparator"),
        std::vector<std::pair<uint16_t, uint16_t>>{{PropIndices::FACING, PropIndices::FACING_WEST},
                                                   {PropIndices::POWERED, PropIndices::POWERED_TRUE},
                                                   {PropIndices::MODE, PropIndices::MODE_SUBTRACT}});
    if (!modelVariantMatches(comparatorWestSubtractPowered, "block/comparator_subtract_powered", 90, 0)) {
        return fail("comparator subtract powered state should resolve to the rotated subtract powered model");
    }

    const BlockStateId pistonExtendedNorth = BlockStateRegistry::getState(
        BlockRegistry::requireIdByName("minecraft:piston"),
        std::vector<std::pair<uint16_t, uint16_t>>{{PropIndices::FACING, PropIndices::FACING_NORTH},
                                                   {PropIndices::EXTENDED, PropIndices::EXTENDED_TRUE}});
    if (!modelVariantMatches(pistonExtendedNorth, "block/piston_extended", 180, 0)) {
        return fail("piston extended north state should resolve to the extended piston model");
    }

    const BlockStateId stickyPistonUp = BlockStateRegistry::getState(
        BlockRegistry::requireIdByName("minecraft:sticky_piston"),
        std::vector<std::pair<uint16_t, uint16_t>>{{PropIndices::FACING, PropIndices::FACING_UP},
                                                   {PropIndices::EXTENDED, PropIndices::EXTENDED_FALSE}});
    if (!modelVariantMatches(stickyPistonUp, "block/sticky_piston", 0, 270)) {
        return fail("sticky_piston up state should resolve to the rotated sticky piston model");
    }

    const BlockStateId stickyPistonHeadEast = BlockStateRegistry::getState(
        BlockRegistry::requireIdByName("minecraft:piston_head"),
        std::vector<std::pair<uint16_t, uint16_t>>{{PropIndices::FACING, PropIndices::FACING_EAST},
                                                   {PropIndices::TYPE, PropIndices::TYPE_STICKY}});
    if (!modelVariantMatches(stickyPistonHeadEast, "block/sticky_piston_head", 270, 0)) {
        return fail("sticky piston head east state should resolve to the sticky head model");
    }

    const BlockStateId observerDownPowered = BlockStateRegistry::getState(
        BlockRegistry::requireIdByName("minecraft:observer"),
        std::vector<std::pair<uint16_t, uint16_t>>{{PropIndices::FACING, PropIndices::FACING_DOWN},
                                                   {PropIndices::POWERED, PropIndices::POWERED_TRUE}});
    if (!modelVariantMatches(observerDownPowered, "block/observer_powered", 0, 90)) {
        return fail("observer down powered state should resolve to the powered observer model");
    }

    const BlockStateId dispenserUp = BlockStateRegistry::getState(BlockRegistry::requireIdByName("minecraft:dispenser"),
                                                                  PropIndices::FACING, PropIndices::FACING_UP);
    if (!modelVariantMatches(dispenserUp, "block/dispenser", 0, 270)) {
        return fail("dispenser up state should resolve to the rotated dispenser model");
    }
    const BlockStateId dispenserUpPowered =
        BlockStateRegistry::withProperty(dispenserUp, PropIndices::POWERED, PropIndices::POWERED_TRUE);
    if (!modelVariantMatches(dispenserUpPowered, "block/dispenser", 0, 270)) {
        return fail("powered dispenser up state should resolve to the rotated dispenser model");
    }

    const BlockStateId dropperEast = BlockStateRegistry::getState(BlockRegistry::requireIdByName("minecraft:dropper"),
                                                                  PropIndices::FACING, PropIndices::FACING_EAST);
    if (!modelVariantMatches(dropperEast, "block/dropper", 270, 0)) {
        return fail("dropper east state should resolve to the rotated dropper model");
    }
    const BlockStateId dropperEastPowered =
        BlockStateRegistry::withProperty(dropperEast, PropIndices::POWERED, PropIndices::POWERED_TRUE);
    if (!modelVariantMatches(dropperEastPowered, "block/dropper", 270, 0)) {
        return fail("powered dropper east state should resolve to the rotated dropper model");
    }

    const BlockStateId hopperSouth = BlockStateRegistry::getState(BlockRegistry::requireIdByName("minecraft:hopper"),
                                                                  PropIndices::FACING, PropIndices::FACING_SOUTH);
    if (!modelVariantMatches(hopperSouth, "block/hopper_side", 180, 0)) {
        return fail("hopper south state should resolve to the rotated side hopper model");
    }
    if (BlockStateRegistry::getPropertyIndex(hopperSouth, PropIndices::ENABLED) != PropIndices::ENABLED_TRUE) {
        return fail("hopper default state should keep enabled=true");
    }

    const BlockStateId hopperDisabledSouth = BlockStateRegistry::getState(
        BlockRegistry::requireIdByName("minecraft:hopper"),
        {{PropIndices::FACING, PropIndices::FACING_SOUTH}, {PropIndices::ENABLED, PropIndices::ENABLED_FALSE}});
    if (!modelVariantMatches(hopperDisabledSouth, "block/hopper_side", 180, 0)) {
        return fail("disabled hopper south state should resolve to the same rotated model");
    }

    const BlockDef& pistonDef = BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:piston"));
    const BlockDef& stickyPistonDef = BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:sticky_piston"));
    if (!pistonDef.revertPlacementFacing || !stickyPistonDef.revertPlacementFacing) {
        return fail("pistons should parse placement facing revert from blocks.json");
    }
    PlacementStrategyFn pistonStrategy = PlacementStrategyRegistry::getStrategy(pistonDef.placementStrategy);
    if (pistonStrategy == nullptr) {
        return fail("six_way_facing placement strategy should be registered");
    }
    PlacementContext pistonPlacement;
    pistonPlacement.blockId = BlockRegistry::requireIdByName("minecraft:piston");
    pistonPlacement.hitNormal = glm::ivec3(0, 1, 0);
    const BlockStateId pistonPlacedUp = pistonStrategy(pistonPlacement);
    if (BlockStateRegistry::getPropertyIndex(pistonPlacedUp, PropIndices::FACING) != PropIndices::FACING_UP) {
        return fail("six_way_facing placement should derive upward piston facing from floor hits");
    }
    pistonPlacement.hitNormal = glm::ivec3(0, -1, 0);
    const BlockStateId pistonPlacedDown = pistonStrategy(pistonPlacement);
    if (BlockStateRegistry::getPropertyIndex(pistonPlacedDown, PropIndices::FACING) != PropIndices::FACING_DOWN) {
        return fail("six_way_facing placement should derive downward piston facing from ceiling hits");
    }
    pistonPlacement.hitNormal = glm::ivec3(1, 0, 0);
    pistonPlacement.playerYaw = 0.0f;
    const BlockStateId pistonPlacedHorizontal = pistonStrategy(pistonPlacement);
    if (BlockStateRegistry::getPropertyIndex(pistonPlacedHorizontal, PropIndices::FACING) != PropIndices::FACING_WEST) {
        return fail("six_way_facing placement should apply the configured piston facing revert");
    }

    const BlockDef& hopperDef = BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:hopper"));
    if (hopperDef.redstoneBehavior != "hopper" || !hopperDef.respondsToRedstone ||
        hopperDef.redstoneControlledProperty != "enabled" || !hopperDef.redstoneControlledPowerInverted) {
        return fail("hopper should declare inverted enabled redstone control metadata");
    }
    if (hopperDef.containerUi != "minecraft:hopper") {
        return fail("hopper should bind to its data-driven container UI");
    }
    PlacementStrategyFn hopperStrategy = PlacementStrategyRegistry::getStrategy(hopperDef.placementStrategy);
    if (hopperStrategy == nullptr) {
        return fail("hopper_facing placement strategy should be registered");
    }
    PlacementContext hopperPlacement;
    hopperPlacement.blockId = BlockRegistry::requireIdByName("minecraft:hopper");
    hopperPlacement.hitNormal = glm::ivec3(0, 1, 0);
    const BlockStateId hopperPlacedDown = hopperStrategy(hopperPlacement);
    if (BlockStateRegistry::getPropertyIndex(hopperPlacedDown, PropIndices::FACING) != PropIndices::FACING_DOWN) {
        return fail("hopper floor placement should produce facing=down");
    }
    hopperPlacement.hitNormal = glm::ivec3(1, 0, 0);
    const BlockStateId hopperPlacedWest = hopperStrategy(hopperPlacement);
    if (BlockStateRegistry::getPropertyIndex(hopperPlacedWest, PropIndices::FACING) != PropIndices::FACING_WEST) {
        return fail("hopper side placement should point into the clicked side");
    }

    if (BlockStateRegistry::getStateCount() <= BlockRegistry::getBlockCount()) {
        return fail("state registry should contain expanded states beyond raw block ids");
    }

    const BlockStateId waterDefault =
        BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:water"));
    if (!FluidState::isWater(waterDefault) ||
        waterDefault.registryIndex() == BlockRegistry::requireIdByName("minecraft:water")) {
        return fail("water should expand into dedicated state ids");
    }
    if (!FluidState::isSource(waterDefault) || FluidState::level(waterDefault) != 0 ||
        FluidState::isFalling(waterDefault)) {
        return fail("default water state should decode as a non-falling source");
    }

    const BlockStateId waterLevel3 = FluidState::makeWater(3, false);
    if (FluidState::level(waterLevel3) != 3 || FluidState::isFalling(waterLevel3)) {
        return fail("water helper should build horizontal decay states");
    }

    const StateTextureIndices& waterDefaultTextures = BlockStateRegistry::getStateTextures(waterDefault);
    if (!waterDefaultTextures.faceTop.isAnimated || waterDefaultTextures.faceTop.frameCount != 32 ||
        waterDefaultTextures.faceTop.fps <= 0.0f) {
        return fail("default water state should expose animated still top-face metadata");
    }
    if (!waterDefaultTextures.faceBottom.isAnimated || waterDefaultTextures.faceBottom.frameCount != 32) {
        return fail("default water state should expose animated still bottom-face metadata");
    }
    if (!waterDefaultTextures.faceFront.isAnimated || waterDefaultTextures.faceFront.frameCount != 32 ||
        waterDefaultTextures.faceFront.fps != waterDefaultTextures.faceTop.fps) {
        return fail("default water state sides should expose animated still metadata");
    }

    const StateTextureIndices& waterLevel3Textures = BlockStateRegistry::getStateTextures(waterLevel3);
    if (!waterLevel3Textures.faceTop.isAnimated || waterLevel3Textures.faceTop.frameCount != 32 ||
        waterLevel3Textures.faceTop.fps != waterDefaultTextures.faceTop.fps) {
        return fail("flowing water top face should stay on the still animation");
    }

    const BlockStateId fallingWater = FluidState::makeWater(0, true);
    if (!FluidState::isFalling(fallingWater) || !FluidState::isWater(fallingWater)) {
        return fail("water helper should build falling water states");
    }

    const StateTextureIndices& fallingWaterTextures = BlockStateRegistry::getStateTextures(fallingWater);
    if (!fallingWaterTextures.faceTop.isAnimated ||
        fallingWaterTextures.faceTop.fps != waterDefaultTextures.faceTop.fps) {
        return fail("falling water top face should stay on the still animation");
    }
    if (!fallingWaterTextures.faceFront.isAnimated ||
        fallingWaterTextures.faceFront.fps != waterDefaultTextures.faceFront.fps) {
        return fail("falling water side faces should stay on the still animation");
    }

    const FluidDesc& waterDesc = FluidRegistry::get(FluidKind::Water);
    if (waterDesc.blockId != BlockRegistry::requireIdByName("minecraft:water") || waterDesc.tickDelay != 5 ||
        waterDesc.maxLevel != 7) {
        return fail("fluid registry should load the configured water descriptor");
    }
    if (waterDesc.slopeSearchDistance != 5 || !waterDesc.canCreateInfiniteSource ||
        waterDesc.infiniteSourceNeighborCount != 2 || !waterDesc.requiresSupportForInfiniteSource) {
        return fail("fluid registry should expose water spread and infinite source settings");
    }

    const DecodedFluid decodedLevel3 = FluidState::decode(waterLevel3);
    if (decodedLevel3.kind != FluidKind::Water || decodedLevel3.level != 3 || decodedLevel3.falling ||
        decodedLevel3.isSource) {
        return fail("fluid decode should preserve water level metadata");
    }
    if (FluidState::encode(decodedLevel3) != waterLevel3) {
        return fail("fluid encode should round-trip decoded water states");
    }
    if (!FluidState::canReplace(waterDesc, NULL_BLOCK_STATE) || !FluidState::canReplace(waterDesc, waterLevel3)) {
        return fail("water should be allowed to replace air and existing water");
    }
    const BlockStateId stoneState =
        BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:stone"));
    if (FluidState::canReplace(waterDesc, stoneState) || FluidState::canCoexist(waterDesc, stoneState)) {
        return fail("waterlogging placeholder APIs should not allow solid coexistence yet");
    }

    {
        SubChunk subChunk;
        std::vector<BlockID> ids;
        ids.reserve(300);
        for (int i = 0; i < 300; ++i) {
            const BlockID id =
                BlockRegistry::registerBlock(NamespacedId("test", "palette_" + std::to_string(i)), BlockDef{});
            ids.push_back(id);

            const int x = i % SubChunk::SIZE;
            const int z = (i / SubChunk::SIZE) % SubChunk::SIZE;
            const int y = i / (SubChunk::SIZE * SubChunk::SIZE);
            subChunk.setBlockFast(x, y, z, BlockStateRegistry::getDefaultState(id));
        }

        for (int i = 0; i < 300; ++i) {
            const int x = i % SubChunk::SIZE;
            const int z = (i / SubChunk::SIZE) % SubChunk::SIZE;
            const int y = i / (SubChunk::SIZE * SubChunk::SIZE);
            if (subChunk.getBlock(x, y, z) != BlockStateRegistry::getDefaultState(ids[i])) {
                return fail("sub-chunk palette should preserve block ids past 256 unique entries");
            }
        }
    }

    std::cout << "[block_state_registry_test] PASS\n";
    return EXIT_SUCCESS;
}
