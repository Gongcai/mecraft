#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "../src/world/block/Block.h"
#include "../src/world/block/BlockModelRegistry.h"
#include "../src/world/block/BlockStateRegistry.h"
#include "../src/world/block/BedBlock.h"
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
}

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
    if (PropIndices::SHAPE == PropIndices::INVALID ||
        PropIndices::SHAPE_STRAIGHT == PropIndices::INVALID ||
        PropIndices::SHAPE_INNER_LEFT == PropIndices::INVALID ||
        PropIndices::SHAPE_INNER_RIGHT == PropIndices::INVALID ||
        PropIndices::SHAPE_OUTER_LEFT == PropIndices::INVALID ||
        PropIndices::SHAPE_OUTER_RIGHT == PropIndices::INVALID) {
        return fail("stair shape properties should be registered from blocks.json");
    }
    if (PropIndices::NORTH == PropIndices::INVALID ||
        PropIndices::SOUTH == PropIndices::INVALID ||
        PropIndices::EAST == PropIndices::INVALID ||
        PropIndices::WEST == PropIndices::INVALID) {
        return fail("connection properties should be registered from blocks.json");
    }
    if (PropIndices::PART == PropIndices::INVALID ||
        PropIndices::PART_HEAD == PropIndices::INVALID ||
        PropIndices::PART_FOOT == PropIndices::INVALID) {
        return fail("bed part properties should be registered from blocks.json");
    }

    const StateID torchDefault = BlockStateRegistry::getDefaultState(BlockIds::TORCH);
    if (torchDefault == BlockIds::TORCH) {
        return fail("torch should expand into dedicated state ids");
    }
    if (BlockStateRegistry::getBlockId(torchDefault) != BlockIds::TORCH) {
        return fail("expanded torch state should resolve back to torch block id");
    }
    if (BlockStateRegistry::getPropertyIndex(torchDefault, PropIndices::FACING) != PropIndices::FACING_FLOOR) {
        return fail("torch default state should face floor");
    }

    const StateID torchNorth = BlockStateRegistry::withProperty(
        torchDefault,
        PropIndices::FACING,
        PropIndices::FACING_NORTH);
    if (torchNorth == torchDefault) {
        return fail("withProperty should produce a distinct torch north-facing state");
    }
    if (BlockStateRegistry::getPropertyIndex(torchNorth, PropIndices::FACING) != PropIndices::FACING_NORTH) {
        return fail("torch north-facing state should report facing=north");
    }
    if (!BlockRegistry::get(torchNorth).isLightSource) {
        return fail("state ids should resolve through BlockRegistry to the owning block definition");
    }
    if (BlockRegistry::getBlockDropId(torchNorth) != BlockRegistry::getBlockDropId(BlockIds::TORCH)) {
        return fail("state ids should reuse the owning block drop table entry");
    }

    const StateID birchLogDefault = BlockStateRegistry::getDefaultState(BlockIds::BIRCH_LOG);
    if (BlockStateRegistry::getPropertyIndex(birchLogDefault, PropIndices::AXIS) != PropIndices::AXIS_Y) {
        return fail("birch log default state should be axis=y");
    }

    const StateID birchLogX = BlockStateRegistry::getState(
        BlockIds::BIRCH_LOG,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::AXIS, PropIndices::AXIS_X}
        });
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
    if (furnace == BlockIds::AIR) {
        return fail("furnace should be registered from blocks.json");
    }
    const BlockDef& furnaceDef = BlockRegistry::get(furnace);
    if (furnaceDef.placementStrategy != "horizontal_facing") {
        return fail("furnace should parse horizontal_facing placement strategy");
    }
    if (!furnaceDef.revertPlacementFacing) {
        return fail("furnace should face back toward the placing player");
    }
    const StateID furnaceSouth = BlockStateRegistry::getState(furnace, PropIndices::FACING, PropIndices::FACING_SOUTH);
    const StateID furnaceNorth = BlockStateRegistry::getState(furnace, PropIndices::FACING, PropIndices::FACING_NORTH);
    const StateID furnaceEast = BlockStateRegistry::getState(furnace, PropIndices::FACING, PropIndices::FACING_EAST);
    const StateID furnaceWest = BlockStateRegistry::getState(furnace, PropIndices::FACING, PropIndices::FACING_WEST);
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
    const StateID furnacePlacedSouth = furnaceStrategy(furnacePlacement);
    if (BlockStateRegistry::getPropertyIndex(furnacePlacedSouth, PropIndices::FACING) != PropIndices::FACING_SOUTH) {
        return fail("furnace placement should face back toward the placing player");
    }

    const std::string torchStateString = BlockStateRegistry::stateToString(torchNorth);
    if (torchStateString.find("facing=north") == std::string::npos) {
        return fail("stateToString should include the resolved torch facing");
    }

    const BlockDef& torchDef = BlockRegistry::get(BlockIds::TORCH);
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

    const BlockDef& birchLogDef = BlockRegistry::get(BlockIds::BIRCH_LOG);
    if (birchLogDef.placementStrategy != "axis_oriented") {
        return fail("birch log should parse axis-oriented placement strategy");
    }

    const BlockID oakStairs = BlockRegistry::findByName("oak_stairs");
    if (oakStairs == BlockIds::AIR) {
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
    const StateID oakStairsSouth = BlockStateRegistry::getState(
        oakStairs,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, PropIndices::FACING_SOUTH},
            {PropIndices::HALF, PropIndices::HALF_BOTTOM},
            {PropIndices::SHAPE, PropIndices::SHAPE_STRAIGHT}
        });
    const ModelVariant* oakStairsVariant = BlockStateRegistry::getModelVariant(oakStairsSouth);
    if (oakStairsVariant == nullptr || oakStairsVariant->model == nullptr ||
        oakStairsVariant->model->name != "block/oak_stairs" ||
        oakStairsVariant->transform.rotY != 90 ||
        !oakStairsVariant->transform.uvLock) {
        return fail("oak_stairs south state should resolve to the rotated oak stairs model");
    }

    const std::vector<std::tuple<uint16_t, uint16_t, uint16_t, const char*, uint16_t, uint16_t, bool>> oakStairsVariantCases = {
        {PropIndices::FACING_EAST, PropIndices::HALF_BOTTOM, PropIndices::SHAPE_STRAIGHT, "block/oak_stairs", 0, 0, false},
        {PropIndices::FACING_EAST, PropIndices::HALF_BOTTOM, PropIndices::SHAPE_INNER_LEFT, "block/oak_stairs_inner", 270, 0, true},
        {PropIndices::FACING_EAST, PropIndices::HALF_BOTTOM, PropIndices::SHAPE_INNER_RIGHT, "block/oak_stairs_inner", 0, 0, false},
        {PropIndices::FACING_EAST, PropIndices::HALF_BOTTOM, PropIndices::SHAPE_OUTER_LEFT, "block/oak_stairs_outer", 270, 0, true},
        {PropIndices::FACING_EAST, PropIndices::HALF_BOTTOM, PropIndices::SHAPE_OUTER_RIGHT, "block/oak_stairs_outer", 0, 0, false},
        {PropIndices::FACING_SOUTH, PropIndices::HALF_BOTTOM, PropIndices::SHAPE_STRAIGHT, "block/oak_stairs", 90, 0, true},
        {PropIndices::FACING_SOUTH, PropIndices::HALF_BOTTOM, PropIndices::SHAPE_INNER_LEFT, "block/oak_stairs_inner", 0, 0, false},
        {PropIndices::FACING_SOUTH, PropIndices::HALF_BOTTOM, PropIndices::SHAPE_INNER_RIGHT, "block/oak_stairs_inner", 90, 0, true},
        {PropIndices::FACING_SOUTH, PropIndices::HALF_BOTTOM, PropIndices::SHAPE_OUTER_LEFT, "block/oak_stairs_outer", 0, 0, false},
        {PropIndices::FACING_SOUTH, PropIndices::HALF_BOTTOM, PropIndices::SHAPE_OUTER_RIGHT, "block/oak_stairs_outer", 90, 0, true},
        {PropIndices::FACING_WEST, PropIndices::HALF_BOTTOM, PropIndices::SHAPE_STRAIGHT, "block/oak_stairs", 180, 0, true},
        {PropIndices::FACING_WEST, PropIndices::HALF_BOTTOM, PropIndices::SHAPE_INNER_LEFT, "block/oak_stairs_inner", 90, 0, true},
        {PropIndices::FACING_WEST, PropIndices::HALF_BOTTOM, PropIndices::SHAPE_INNER_RIGHT, "block/oak_stairs_inner", 180, 0, true},
        {PropIndices::FACING_WEST, PropIndices::HALF_BOTTOM, PropIndices::SHAPE_OUTER_LEFT, "block/oak_stairs_outer", 90, 0, true},
        {PropIndices::FACING_WEST, PropIndices::HALF_BOTTOM, PropIndices::SHAPE_OUTER_RIGHT, "block/oak_stairs_outer", 180, 0, true},
        {PropIndices::FACING_NORTH, PropIndices::HALF_BOTTOM, PropIndices::SHAPE_STRAIGHT, "block/oak_stairs", 270, 0, true},
        {PropIndices::FACING_NORTH, PropIndices::HALF_BOTTOM, PropIndices::SHAPE_INNER_LEFT, "block/oak_stairs_inner", 180, 0, true},
        {PropIndices::FACING_NORTH, PropIndices::HALF_BOTTOM, PropIndices::SHAPE_INNER_RIGHT, "block/oak_stairs_inner", 270, 0, true},
        {PropIndices::FACING_NORTH, PropIndices::HALF_BOTTOM, PropIndices::SHAPE_OUTER_LEFT, "block/oak_stairs_outer", 180, 0, true},
        {PropIndices::FACING_NORTH, PropIndices::HALF_BOTTOM, PropIndices::SHAPE_OUTER_RIGHT, "block/oak_stairs_outer", 270, 0, true},
        {PropIndices::FACING_EAST, PropIndices::HALF_TOP, PropIndices::SHAPE_STRAIGHT, "block/oak_stairs", 0, 180, true},
        {PropIndices::FACING_EAST, PropIndices::HALF_TOP, PropIndices::SHAPE_INNER_LEFT, "block/oak_stairs_inner", 270, 180, true},
        {PropIndices::FACING_EAST, PropIndices::HALF_TOP, PropIndices::SHAPE_INNER_RIGHT, "block/oak_stairs_inner", 0, 180, true},
        {PropIndices::FACING_EAST, PropIndices::HALF_TOP, PropIndices::SHAPE_OUTER_LEFT, "block/oak_stairs_outer", 270, 180, true},
        {PropIndices::FACING_EAST, PropIndices::HALF_TOP, PropIndices::SHAPE_OUTER_RIGHT, "block/oak_stairs_outer", 0, 180, true},
        {PropIndices::FACING_SOUTH, PropIndices::HALF_TOP, PropIndices::SHAPE_STRAIGHT, "block/oak_stairs", 90, 180, true},
        {PropIndices::FACING_SOUTH, PropIndices::HALF_TOP, PropIndices::SHAPE_INNER_LEFT, "block/oak_stairs_inner", 0, 180, true},
        {PropIndices::FACING_SOUTH, PropIndices::HALF_TOP, PropIndices::SHAPE_INNER_RIGHT, "block/oak_stairs_inner", 90, 180, true},
        {PropIndices::FACING_SOUTH, PropIndices::HALF_TOP, PropIndices::SHAPE_OUTER_LEFT, "block/oak_stairs_outer", 0, 180, true},
        {PropIndices::FACING_SOUTH, PropIndices::HALF_TOP, PropIndices::SHAPE_OUTER_RIGHT, "block/oak_stairs_outer", 90, 180, true},
        {PropIndices::FACING_WEST, PropIndices::HALF_TOP, PropIndices::SHAPE_STRAIGHT, "block/oak_stairs", 180, 180, true},
        {PropIndices::FACING_WEST, PropIndices::HALF_TOP, PropIndices::SHAPE_INNER_LEFT, "block/oak_stairs_inner", 90, 180, true},
        {PropIndices::FACING_WEST, PropIndices::HALF_TOP, PropIndices::SHAPE_INNER_RIGHT, "block/oak_stairs_inner", 180, 180, true},
        {PropIndices::FACING_WEST, PropIndices::HALF_TOP, PropIndices::SHAPE_OUTER_LEFT, "block/oak_stairs_outer", 90, 180, true},
        {PropIndices::FACING_WEST, PropIndices::HALF_TOP, PropIndices::SHAPE_OUTER_RIGHT, "block/oak_stairs_outer", 180, 180, true},
        {PropIndices::FACING_NORTH, PropIndices::HALF_TOP, PropIndices::SHAPE_STRAIGHT, "block/oak_stairs", 270, 180, true},
        {PropIndices::FACING_NORTH, PropIndices::HALF_TOP, PropIndices::SHAPE_INNER_LEFT, "block/oak_stairs_inner", 180, 180, true},
        {PropIndices::FACING_NORTH, PropIndices::HALF_TOP, PropIndices::SHAPE_INNER_RIGHT, "block/oak_stairs_inner", 270, 180, true},
        {PropIndices::FACING_NORTH, PropIndices::HALF_TOP, PropIndices::SHAPE_OUTER_LEFT, "block/oak_stairs_outer", 180, 180, true},
        {PropIndices::FACING_NORTH, PropIndices::HALF_TOP, PropIndices::SHAPE_OUTER_RIGHT, "block/oak_stairs_outer", 270, 180, true},
    };
    for (const auto& [facing, half, shape, expectedModel, expectedRotY, expectedRotX, expectedUvLock] : oakStairsVariantCases) {
        const StateID state = BlockStateRegistry::getState(
            oakStairs,
            std::vector<std::pair<uint16_t, uint16_t>>{
                {PropIndices::FACING, facing},
                {PropIndices::HALF, half},
                {PropIndices::SHAPE, shape}
            });
        const ModelVariant* variant = BlockStateRegistry::getModelVariant(state);
        if (variant == nullptr || variant->model == nullptr ||
            variant->model->name != expectedModel ||
            variant->transform.rotY != expectedRotY ||
            variant->transform.rotX != expectedRotX ||
            variant->transform.uvLock != expectedUvLock) {
            return fail("all oak_stairs facing/half/shape states should resolve to their JSON model transforms");
        }
    }

    const BlockID redBed = BlockRegistry::findByName("red_bed");
    if (redBed == BlockIds::AIR) {
        return fail("red_bed should be registered from blocks.json");
    }
    const BlockDef& redBedDef = BlockRegistry::get(redBed);
    if (redBedDef.renderShapeName != "model" ||
        redBedDef.renderShapeTag != MeshBuilderRegistry::getShapeTag("model")) {
        return fail("red_bed should use the model mesh builder");
    }
    if (redBedDef.placementStrategy != "bed" ||
        redBedDef.isSolid ||
        redBedDef.isTransparent ||
        redBedDef.renderLayer != BlockRenderLayer::Opaque ||
        redBedDef.opacity != 0) {
        return fail("red_bed should parse bed placement and opaque non-full-block properties");
    }
    if (!BedBlockLogic::isBedBlock(redBed)) {
        return fail("red_bed should be recognized by bed block logic");
    }
    if (BedBlockLogic::isBedState(redBed)) {
        return fail("raw red_bed block id should not be treated as a complete bed state");
    }

    const StateID redBedDefault = BlockStateRegistry::getDefaultState(redBed);
    if (BlockStateRegistry::getBlockId(redBedDefault) != redBed ||
        BlockStateRegistry::getPropertyIndex(redBedDefault, PropIndices::FACING) != PropIndices::FACING_EAST ||
        BlockStateRegistry::getPropertyIndex(redBedDefault, PropIndices::PART) != PropIndices::PART_FOOT ||
        !BedBlockLogic::isFootState(redBedDefault)) {
        return fail("red_bed default state should be east-facing foot");
    }

    const StateID redBedHead = BedBlockLogic::makeBedState(redBed, PropIndices::FACING_EAST, PropIndices::PART_HEAD);
    if (!BedBlockLogic::isHeadState(redBedHead) ||
        BlockStateRegistry::getPropertyIndex(redBedHead, PropIndices::PART) != PropIndices::PART_HEAD) {
        return fail("bed helper should construct head states");
    }
    const ModelVariant* redBedFootVariant = BlockStateRegistry::getModelVariant(redBedDefault);
    const ModelVariant* redBedHeadVariant = BlockStateRegistry::getModelVariant(redBedHead);
    if (redBedFootVariant == nullptr ||
        redBedFootVariant->model == nullptr ||
        redBedHeadVariant == nullptr ||
        redBedHeadVariant->model == nullptr ||
        redBedFootVariant->model->elements.empty() ||
        redBedHeadVariant->model->elements.empty()) {
        return fail("red_bed foot and head states should resolve to model elements");
    }
    const auto bedBodyUvMatches = [](const std::unique_ptr<ModelFace>& face) {
        return face != nullptr &&
               face->uv[0] == 0.0f &&
               face->uv[1] == 3.0f &&
               face->uv[2] == 16.0f &&
               face->uv[3] == 9.0f;
    };
    const auto bedBodyNorthUvMatches = [](const std::unique_ptr<ModelFace>& face) {
        return face != nullptr &&
               face->uv[0] == 16.0f &&
               face->uv[1] == 3.0f &&
               face->uv[2] == 0.0f &&
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
        const StateID state = BlockStateRegistry::getState(
            redBed,
            std::vector<std::pair<uint16_t, uint16_t>>{
                {PropIndices::FACING, facing},
                {PropIndices::PART, part}
            });
        const ModelVariant* variant = BlockStateRegistry::getModelVariant(state);
        if (variant == nullptr || variant->model == nullptr ||
            variant->model->name != expectedModel ||
            variant->transform.rotY != expectedRotY ||
            variant->transform.rotX != 0 ||
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
    const StateID redBedPlacedWest = bedStrategy(bedPlacement);
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
    if (oakSlab == BlockIds::AIR) {
        return fail("oak_slab should be registered from blocks.json");
    }
    const BlockDef& oakSlabDef = BlockRegistry::get(oakSlab);
    if (oakSlabDef.placementStrategy != "slab") {
        return fail("oak_slab should parse slab placement strategy");
    }
    if (PropIndices::HALF_DOUBLE == PropIndices::INVALID) {
        return fail("half=double should be registered for stacked slabs");
    }
    const StateID oakSlabTop = BlockStateRegistry::getState(
        oakSlab,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::HALF, PropIndices::HALF_TOP}
        });
    const ModelVariant* oakSlabVariant = BlockStateRegistry::getModelVariant(oakSlabTop);
    if (oakSlabVariant == nullptr || oakSlabVariant->model == nullptr ||
        oakSlabVariant->model->name != "block/oak_slab_top") {
        return fail("oak_slab top state should resolve to the top slab model");
    }
    const StateID oakSlabDouble = BlockStateRegistry::getState(
        oakSlab,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::HALF, PropIndices::HALF_DOUBLE}
        });
    const ModelVariant* oakSlabDoubleVariant = BlockStateRegistry::getModelVariant(oakSlabDouble);
    if (oakSlabDoubleVariant == nullptr || oakSlabDoubleVariant->model == nullptr ||
        oakSlabDoubleVariant->model->name != "block/oak_slab_double") {
        return fail("oak_slab double state should resolve to the full slab model");
    }

    const BlockID oakVerticalSlab = BlockRegistry::findByName("oak_vertical_slab");
    if (oakVerticalSlab == BlockIds::AIR) {
        return fail("oak_vertical_slab should be registered from blocks.json");
    }
    const BlockDef& oakVerticalSlabDef = BlockRegistry::get(oakVerticalSlab);
    if (oakVerticalSlabDef.placementStrategy != "vertical_slab") {
        return fail("oak_vertical_slab should parse vertical slab placement strategy");
    }
    if (PropIndices::HALF_NORTH == PropIndices::INVALID ||
        PropIndices::HALF_SOUTH == PropIndices::INVALID ||
        PropIndices::HALF_EAST == PropIndices::INVALID ||
        PropIndices::HALF_WEST == PropIndices::INVALID) {
        return fail("horizontal half values should be registered for vertical slabs");
    }
    const StateID oakVerticalSlabEast = BlockStateRegistry::getState(
        oakVerticalSlab,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::HALF, PropIndices::HALF_EAST}
        });
    const ModelVariant* oakVerticalSlabEastVariant = BlockStateRegistry::getModelVariant(oakVerticalSlabEast);
    if (oakVerticalSlabEastVariant == nullptr || oakVerticalSlabEastVariant->model == nullptr ||
        oakVerticalSlabEastVariant->model->name != "block/oak_vertical_slab_east") {
        return fail("oak_vertical_slab east state should resolve to the east vertical slab model");
    }
    const StateID oakVerticalSlabWest = BlockStateRegistry::getState(
        oakVerticalSlab,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::HALF, PropIndices::HALF_WEST}
        });
    const StateID oakVerticalSlabNorth = BlockStateRegistry::getState(
        oakVerticalSlab,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::HALF, PropIndices::HALF_NORTH}
        });
    const StateID oakVerticalSlabSouth = BlockStateRegistry::getState(
        oakVerticalSlab,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::HALF, PropIndices::HALF_SOUTH}
        });
    const StateID oakVerticalSlabDouble = BlockStateRegistry::getState(
        oakVerticalSlab,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::HALF, PropIndices::HALF_DOUBLE}
        });
    const ModelVariant* oakVerticalSlabDoubleVariant = BlockStateRegistry::getModelVariant(oakVerticalSlabDouble);
    if (oakVerticalSlabDoubleVariant == nullptr || oakVerticalSlabDoubleVariant->model == nullptr ||
        oakVerticalSlabDoubleVariant->model->name != "block/oak_slab_double") {
        return fail("oak_vertical_slab double state should resolve to the full slab model");
    }

    const BlockID cauldron = BlockRegistry::findByName("cauldron");
    if (cauldron == BlockIds::AIR) {
        return fail("cauldron should be registered from blocks.json");
    }
    const BlockDef& cauldronDef = BlockRegistry::get(cauldron);
    if (cauldronDef.renderShapeName != "model" ||
        cauldronDef.renderShapeTag != MeshBuilderRegistry::getShapeTag("model")) {
        return fail("cauldron should use the model mesh builder");
    }
    const StateID cauldronDefault = BlockStateRegistry::getDefaultState(cauldron);
    const ModelVariant* cauldronVariant = BlockStateRegistry::getModelVariant(cauldronDefault);
    if (cauldronVariant == nullptr || cauldronVariant->model == nullptr ||
        cauldronVariant->model->name != "block/cauldron" ||
        cauldronVariant->model->elements.size() != 13) {
        return fail("cauldron default state should resolve to the multi-element cauldron model");
    }

    const BlockID oakFence = BlockRegistry::findByName("oak_fence");
    if (oakFence == BlockIds::AIR) {
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
    const StateID oakFenceEastWest = BlockStateRegistry::getState(
        oakFence,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::NORTH, PropIndices::NORTH_FALSE},
            {PropIndices::SOUTH, PropIndices::SOUTH_FALSE},
            {PropIndices::EAST, PropIndices::EAST_TRUE},
            {PropIndices::WEST, PropIndices::WEST_TRUE}
        });
    const ModelVariant* oakFenceVariant = BlockStateRegistry::getModelVariant(oakFenceEastWest);
    if (oakFenceVariant == nullptr || oakFenceVariant->model == nullptr ||
        oakFenceVariant->model->name != "block/oak_fence_east_west" ||
        oakFenceVariant->model->elements.size() != 5) {
        return fail("oak_fence east/west state should resolve to the connected fence model");
    }

    const BlockID cobblestoneWall = BlockRegistry::findByName("cobblestone_wall");
    if (cobblestoneWall == BlockIds::AIR) {
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
    const StateID cobblestoneWallNorthSouth = BlockStateRegistry::getState(
        cobblestoneWall,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::NORTH, PropIndices::NORTH_TRUE},
            {PropIndices::SOUTH, PropIndices::SOUTH_TRUE},
            {PropIndices::EAST, PropIndices::EAST_FALSE},
            {PropIndices::WEST, PropIndices::WEST_FALSE}
        });
    const ModelVariant* cobblestoneWallVariant = BlockStateRegistry::getModelVariant(cobblestoneWallNorthSouth);
    if (cobblestoneWallVariant == nullptr || cobblestoneWallVariant->model == nullptr ||
        cobblestoneWallVariant->model->name != "block/cobblestone_wall_north_south" ||
        cobblestoneWallVariant->model->elements.size() != 3) {
        return fail("cobblestone_wall north/south state should resolve to the connected wall model");
    }

    const BlockID anvil = BlockRegistry::findByName("anvil");
    if (anvil == BlockIds::AIR) {
        return fail("anvil should be registered from blocks.json");
    }
    const BlockDef& anvilDef = BlockRegistry::get(anvil);
    if (anvilDef.renderShapeName != "model" ||
        anvilDef.renderShapeTag != MeshBuilderRegistry::getShapeTag("model")) {
        return fail("anvil should use the model mesh builder");
    }
    if (anvilDef.placementStrategy != "horizontal_facing") {
        return fail("anvil should parse horizontal_facing placement strategy");
    }
    const StateID anvilEast = BlockStateRegistry::getState(
        anvil,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, PropIndices::FACING_EAST}
        });
    const ModelVariant* anvilVariant = BlockStateRegistry::getModelVariant(anvilEast);
    if (anvilVariant == nullptr || anvilVariant->model == nullptr ||
        anvilVariant->model->name != "block/anvil" ||
        anvilVariant->model->elements.size() != 4 ||
        anvilVariant->transform.rotY != 90) {
        return fail("anvil east state should resolve to the rotated anvil model");
    }

    PlacementStrategyFn torchStrategy = PlacementStrategyRegistry::getStrategy(torchDef.placementStrategy);
    if (torchStrategy == nullptr) {
        return fail("torch placement strategy should be registered");
    }

    PlacementContext torchPlacement;
    torchPlacement.blockId = BlockIds::TORCH;
    torchPlacement.hitNormal = {0, 0, -1};
    const StateID torchPlacedNorth = torchStrategy(torchPlacement);
    if (BlockStateRegistry::getPropertyIndex(torchPlacedNorth, PropIndices::FACING) != PropIndices::FACING_NORTH) {
        return fail("attach_wall placement should derive torch facing from hit normal");
    }

    PlacementStrategyFn logStrategy = PlacementStrategyRegistry::getStrategy(birchLogDef.placementStrategy);
    if (logStrategy == nullptr) {
        return fail("birch log placement strategy should be registered");
    }

    PlacementContext logPlacement;
    logPlacement.blockId = BlockIds::BIRCH_LOG;
    logPlacement.hitNormal = {1, 0, 0};
    const StateID birchLogPlacedX = logStrategy(logPlacement);
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
    const StateID slabPlacedBottom = slabStrategy(slabBottomPlacement);
    if (BlockStateRegistry::getPropertyIndex(slabPlacedBottom, PropIndices::HALF) != PropIndices::HALF_BOTTOM) {
        return fail("slab placement should place bottom slabs on upward-facing hits");
    }

    PlacementContext slabTopPlacement;
    slabTopPlacement.blockId = oakSlab;
    slabTopPlacement.hitNormal = {0, -1, 0};
    const StateID slabPlacedTop = slabStrategy(slabTopPlacement);
    if (BlockStateRegistry::getPropertyIndex(slabPlacedTop, PropIndices::HALF) != PropIndices::HALF_TOP) {
        return fail("slab placement should place top slabs on downward-facing hits");
    }

    StateID slabMerged = BlockIds::AIR;
    if (!tryMergePlacementStates(slabPlacedBottom, slabPlacedTop, slabMerged) ||
        slabMerged != oakSlabDouble) {
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
    const StateID verticalSlabSidePlaced = verticalSlabStrategy(verticalSlabSidePlacement);
    if (BlockStateRegistry::getPropertyIndex(verticalSlabSidePlaced, PropIndices::HALF) != PropIndices::HALF_WEST) {
        return fail("vertical slab side placement should occupy the half touching the clicked face");
    }

    PlacementContext verticalSlabTopPlacement;
    verticalSlabTopPlacement.blockId = oakVerticalSlab;
    verticalSlabTopPlacement.hitNormal = {0, 1, 0};
    verticalSlabTopPlacement.hitPosition = {12.75f, 64.0f, 8.5f};
    const StateID verticalSlabTopPlaced = verticalSlabStrategy(verticalSlabTopPlacement);
    if (BlockStateRegistry::getPropertyIndex(verticalSlabTopPlaced, PropIndices::HALF) != PropIndices::HALF_EAST) {
        return fail("vertical slab top placement should use hit position to select the east half");
    }

    verticalSlabTopPlacement.isSneaking = true;
    const StateID verticalSlabSneakPlaced = verticalSlabStrategy(verticalSlabTopPlacement);
    if (BlockStateRegistry::getPropertyIndex(verticalSlabSneakPlaced, PropIndices::HALF) != PropIndices::HALF_WEST) {
        return fail("sneaking vertical slab placement should invert the selected horizontal half");
    }

    StateID verticalSlabMerged = BlockIds::AIR;
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
    const StateID slabSidePlacedTop = slabStrategy(slabSideTopPlacement);
    if (BlockStateRegistry::getPropertyIndex(slabSidePlacedTop, PropIndices::HALF) != PropIndices::HALF_TOP) {
        return fail("slab side placement should use the hit position to select the top half");
    }

    PlacementContext slabSideBottomPlacement;
    slabSideBottomPlacement.blockId = oakSlab;
    slabSideBottomPlacement.hitNormal = {-1, 0, 0};
    slabSideBottomPlacement.hitPosition = {12.0f, 64.25f, 8.5f};
    const StateID slabSidePlacedBottom = slabStrategy(slabSideBottomPlacement);
    if (BlockStateRegistry::getPropertyIndex(slabSidePlacedBottom, PropIndices::HALF) != PropIndices::HALF_BOTTOM) {
        return fail("slab side placement should use the hit position to select the bottom half");
    }

    slabTopPlacement.isSneaking = true;
    const StateID slabSneakPlacedBottom = slabStrategy(slabTopPlacement);
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
    const StateID stairsPlacedNorth = stairsStrategy(stairsNorthPlacement);
    if (BlockStateRegistry::getPropertyIndex(stairsPlacedNorth, PropIndices::FACING) != PropIndices::FACING_NORTH ||
        BlockStateRegistry::getPropertyIndex(stairsPlacedNorth, PropIndices::HALF) != PropIndices::HALF_BOTTOM ||
        BlockStateRegistry::getPropertyIndex(stairsPlacedNorth, PropIndices::SHAPE) != PropIndices::SHAPE_STRAIGHT) {
        return fail("stairs placement should derive north-facing bottom stairs from camera yaw and top-face hits");
    }

    PlacementContext stairsEastNorthBoundaryPlacement;
    stairsEastNorthBoundaryPlacement.blockId = oakStairs;
    stairsEastNorthBoundaryPlacement.playerYaw = 315.0f;
    stairsEastNorthBoundaryPlacement.hitNormal = {0, 1, 0};
    const StateID stairsPlacedEastNorthBoundary = stairsStrategy(stairsEastNorthBoundaryPlacement);
    if (BlockStateRegistry::getPropertyIndex(stairsPlacedEastNorthBoundary, PropIndices::FACING) != PropIndices::FACING_EAST) {
        return fail("stairs placement should classify the +X/-Z yaw boundary as east-facing");
    }

    PlacementContext stairsSideTopPlacement;
    stairsSideTopPlacement.blockId = oakStairs;
    stairsSideTopPlacement.playerYaw = 0.0f;
    stairsSideTopPlacement.hitNormal = {0, 0, 1};
    stairsSideTopPlacement.hitPosition = {4.5f, 32.75f, 4.0f};
    const StateID stairsPlacedNorthTop = stairsStrategy(stairsSideTopPlacement);
    if (BlockStateRegistry::getPropertyIndex(stairsPlacedNorthTop, PropIndices::FACING) != PropIndices::FACING_NORTH ||
        BlockStateRegistry::getPropertyIndex(stairsPlacedNorthTop, PropIndices::HALF) != PropIndices::HALF_TOP ||
        BlockStateRegistry::getPropertyIndex(stairsPlacedNorthTop, PropIndices::SHAPE) != PropIndices::SHAPE_STRAIGHT) {
        return fail("stairs side placement should reverse the side normal for stair descent direction and derive half from hit position");
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
        const StateID placed = stairsStrategy(sidePlacement);
        if (BlockStateRegistry::getPropertyIndex(placed, PropIndices::FACING) != expectedFacing ||
            BlockStateRegistry::getPropertyIndex(placed, PropIndices::HALF) != PropIndices::HALF_BOTTOM ||
            BlockStateRegistry::getPropertyIndex(placed, PropIndices::SHAPE) != PropIndices::SHAPE_STRAIGHT) {
            return fail("stairs side placement should use the configured facing revert for stair descent direction");
        }
    }

    const StateID chestDefault = BlockStateRegistry::getDefaultState(BlockIds::CHEST);
    if (chestDefault == BlockIds::CHEST) {
        return fail("chest should expand into dedicated facing state ids");
    }
    if (BlockStateRegistry::getBlockId(chestDefault) != BlockIds::CHEST) {
        return fail("expanded chest state should resolve back to chest block id");
    }
    if (BlockStateRegistry::getPropertyIndex(chestDefault, PropIndices::FACING) != PropIndices::FACING_SOUTH) {
        return fail("chest default state should face south");
    }

    const BlockDef& chestDef = BlockRegistry::get(BlockIds::CHEST);
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

    PlacementStrategyFn chestStrategy = PlacementStrategyRegistry::getStrategy(chestDef.placementStrategy);
    if (chestStrategy == nullptr) {
        return fail("chest placement strategy should be registered");
    }

    PlacementContext chestPlacement;
    chestPlacement.blockId = BlockIds::CHEST;
    chestPlacement.playerYaw = 180.0f;
    const StateID chestPlacedEast = chestStrategy(chestPlacement);
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
    const StateID anvilPlacedNorth = anvilStrategy(anvilPlacement);
    if (BlockStateRegistry::getPropertyIndex(anvilPlacedNorth, PropIndices::FACING) != PropIndices::FACING_NORTH) {
        return fail("anvil placement should derive facing from player yaw");
    }

    PlacementStrategyFn fenceStrategy = PlacementStrategyRegistry::getStrategy(oakFenceDef.placementStrategy);
    if (fenceStrategy == nullptr) {
        return fail("fence placement strategy should be registered");
    }

    PlacementContext fencePlacement;
    fencePlacement.blockId = oakFence;
    const StateID fencePlaced = fenceStrategy(fencePlacement);
    if (fencePlaced != BlockStateRegistry::getDefaultState(oakFence)) {
        return fail("fence placement should start from the default disconnected state");
    }

    PlacementStrategyFn wallStrategy = PlacementStrategyRegistry::getStrategy(cobblestoneWallDef.placementStrategy);
    if (wallStrategy == nullptr) {
        return fail("wall placement strategy should be registered");
    }

    PlacementContext wallPlacement;
    wallPlacement.blockId = cobblestoneWall;
    const StateID wallPlaced = wallStrategy(wallPlacement);
    if (wallPlaced != BlockStateRegistry::getDefaultState(cobblestoneWall)) {
        return fail("wall placement should start from the default disconnected state");
    }

    const uint16_t ageProperty = BlockStateRegistry::getPropertyNameIndex("age");
    if (ageProperty == BlockStateRegistry::INVALID_INDEX) {
        return fail("crop age property should be registered from blocks.json");
    }

    const BlockID farmland = BlockRegistry::findByName("farmland");
    if (farmland == BlockIds::AIR) {
        return fail("farmland should be registered for crop support");
    }
    const uint16_t moistureProperty = BlockStateRegistry::getPropertyNameIndex("moisture");
    if (moistureProperty == BlockStateRegistry::INVALID_INDEX) {
        return fail("farmland moisture property should be registered from blocks.json");
    }
    const StateID farmlandDefault = BlockStateRegistry::getDefaultState(farmland);
    if (BlockStateRegistry::getPropertyIndex(farmlandDefault, moistureProperty) !=
        BlockStateRegistry::getPropertyValueIndex(moistureProperty, "0")) {
        return fail("farmland default moisture should be 0");
    }
    const StateID farmlandMoist = BlockStateRegistry::withProperty(
        farmlandDefault,
        moistureProperty,
        BlockStateRegistry::getPropertyValueIndex(moistureProperty, "7"));
    if (farmlandMoist == farmlandDefault ||
        BlockStateRegistry::getBlockId(farmlandMoist) != farmland) {
        return fail("farmland moisture 7 should resolve to a distinct farmland state");
    }
    if (!BlockRegistry::get(farmland).randomTick.enabled ||
        BlockRegistry::get(farmland).randomTick.behavior != "farmland_moisture") {
        return fail("farmland should register its random tick moisture behavior");
    }

    const BlockID wheat = BlockRegistry::findByName("wheat");
    if (wheat == BlockIds::AIR) {
        return fail("wheat crop block should be registered");
    }
    const BlockDef& wheatDef = BlockRegistry::get(wheat);
    if (wheatDef.renderShapeName != "cross" ||
        wheatDef.renderShapeTag != MeshBuilderRegistry::getShapeTag("cross")) {
        return fail("wheat crop should use cross rendering");
    }
    if (wheatDef.supportRule != "farmland") {
        return fail("wheat crop should require farmland support");
    }
    if (wheatDef.stateTextureRules.size() != 1 ||
        wheatDef.stateTextureRules.front().propertyName != "age" ||
        wheatDef.stateTextureRules.front().texturesByValue.size() != 8) {
        return fail("wheat crop should register age texture rules");
    }
    if (!wheatDef.randomTick.enabled ||
        wheatDef.randomTick.behavior != "increment_property" ||
        wheatDef.randomTick.propertyName != "age") {
        return fail("wheat crop should register generic random tick age increment behavior");
    }
    const StateID wheatDefault = BlockStateRegistry::getDefaultState(wheat);
    if (BlockStateRegistry::getPropertyIndex(wheatDefault, ageProperty) !=
        BlockStateRegistry::getPropertyValueIndex(ageProperty, "0")) {
        return fail("wheat crop default age should be 0");
    }
    const StateID wheatMature = BlockStateRegistry::getState(
        wheat,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {ageProperty, BlockStateRegistry::getPropertyValueIndex(ageProperty, "7")}
        });
    if (wheatMature == wheatDefault ||
        BlockStateRegistry::getBlockId(wheatMature) != wheat) {
        return fail("wheat crop age 7 should resolve to a distinct wheat state");
    }

    const BlockID carrots = BlockRegistry::findByName("carrots");
    const BlockID potatoes = BlockRegistry::findByName("potatoes");
    if (carrots == BlockIds::AIR || potatoes == BlockIds::AIR) {
        return fail("carrot and potato crop blocks should be registered");
    }
    const BlockDef& carrotsDef = BlockRegistry::get(carrots);
    const BlockDef& potatoesDef = BlockRegistry::get(potatoes);
    if (carrotsDef.stateTextureRules.size() != 1 ||
        potatoesDef.stateTextureRules.size() != 1 ||
        carrotsDef.stateTextureRules.front().texturesByValue.size() != 4 ||
        potatoesDef.stateTextureRules.front().texturesByValue.size() != 4) {
        return fail("carrot and potato crops should register four age texture rules");
    }

    const BlockID vine = BlockRegistry::findByName("vine");
    if (vine == BlockIds::AIR) {
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
    const StateID northVinePlaced = vineStrategy(vinePlacement);
    if (BlockStateRegistry::getPropertyIndex(northVinePlaced, PropIndices::FACING) != PropIndices::FACING_NORTH) {
        return fail("wall face plane placement should derive facing from the clicked side");
    }
    vinePlacement.hitNormal = glm::ivec3(0, 1, 0);
    if (vineStrategy(vinePlacement) != BlockIds::AIR) {
        return fail("wall face plane placement should reject floor hits");
    }

    const BlockID pinkPetals = BlockRegistry::findByName("pink_petals");
    if (pinkPetals == BlockIds::AIR) {
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
    const StateID floorPetalsPlaced = petalStrategy(petalPlacement);
    if (BlockStateRegistry::getPropertyIndex(floorPetalsPlaced, PropIndices::FACING) != PropIndices::FACING_FLOOR) {
        return fail("floor face plane placement should use the floor facing state");
    }
    petalPlacement.hitNormal = glm::ivec3(1, 0, 0);
    if (petalStrategy(petalPlacement) != BlockIds::AIR) {
        return fail("floor face plane placement should reject wall hits");
    }

    const BlockID wildflowers = BlockRegistry::findByName("wildflowers");
    const BlockID leafLitter = BlockRegistry::findByName("leaf_litter");
    const BlockID glowLichen = BlockRegistry::findByName("glow_lichen");
    if (wildflowers == BlockIds::AIR || leafLitter == BlockIds::AIR || glowLichen == BlockIds::AIR) {
        return fail("new face plane decoration blocks should be registered");
    }

    const BlockDef& wildflowersDef = BlockRegistry::get(wildflowers);
    const BlockDef& leafLitterDef = BlockRegistry::get(leafLitter);
    const BlockDef& glowLichenDef = BlockRegistry::get(glowLichen);
    if (wildflowersDef.renderShapeName != "face_plane" ||
        leafLitterDef.renderShapeName != "face_plane" ||
        glowLichenDef.renderShapeName != "face_plane") {
        return fail("new decoration blocks should use the face plane render shape");
    }
    if (wildflowersDef.placementStrategy != "face_plane_floor" ||
        leafLitterDef.placementStrategy != "face_plane_floor" ||
        glowLichenDef.placementStrategy != "face_plane_wall") {
        return fail("new face plane decorations should use the matching placement strategy");
    }
    if (glowLichenDef.lightLevel != 7) {
        return fail("glow_lichen should emit level 7 block light");
    }
    if (BlockStateRegistry::getPropertyIndex(BlockStateRegistry::getDefaultState(wildflowers),
                                             PropIndices::FACING) != PropIndices::FACING_FLOOR ||
        BlockStateRegistry::getPropertyIndex(BlockStateRegistry::getDefaultState(leafLitter),
                                             PropIndices::FACING) != PropIndices::FACING_FLOOR) {
        return fail("floor face plane decorations should default to facing=floor");
    }
    if (BlockStateRegistry::getState(glowLichen, PropIndices::FACING, PropIndices::FACING_NORTH) == BlockIds::AIR ||
        BlockStateRegistry::getState(glowLichen, PropIndices::FACING, PropIndices::FACING_SOUTH) == BlockIds::AIR ||
        BlockStateRegistry::getState(glowLichen, PropIndices::FACING, PropIndices::FACING_EAST) == BlockIds::AIR ||
        BlockStateRegistry::getState(glowLichen, PropIndices::FACING, PropIndices::FACING_WEST) == BlockIds::AIR) {
        return fail("glow_lichen should expose all horizontal wall facing states");
    }

    if (PropIndices::EXTENDED == PropIndices::INVALID ||
        PropIndices::EXTENDED_TRUE == PropIndices::INVALID ||
        PropIndices::EXTENDED_FALSE == PropIndices::INVALID ||
        PropIndices::POWER == PropIndices::INVALID ||
        PropIndices::POWER_0 == PropIndices::INVALID ||
        PropIndices::POWER_15 == PropIndices::INVALID ||
        PropIndices::TYPE == PropIndices::INVALID ||
        PropIndices::TYPE_NORMAL == PropIndices::INVALID ||
        PropIndices::TYPE_STICKY == PropIndices::INVALID ||
        PropIndices::DELAY == PropIndices::INVALID ||
        PropIndices::DELAY_1 == PropIndices::INVALID ||
        PropIndices::DELAY_2 == PropIndices::INVALID ||
        PropIndices::DELAY_3 == PropIndices::INVALID ||
        PropIndices::DELAY_4 == PropIndices::INVALID ||
        PropIndices::MODE == PropIndices::INVALID ||
        PropIndices::MODE_COMPARE == PropIndices::INVALID ||
        PropIndices::MODE_SUBTRACT == PropIndices::INVALID ||
        PropIndices::FACING_UP == PropIndices::INVALID ||
        PropIndices::FACING_DOWN == PropIndices::INVALID) {
        return fail("redstone support properties should be registered from blocks.json");
    }

    const std::vector<std::pair<BlockID, const char*>> redstoneBlocks = {
        {BlockIds::REDSTONE_WIRE, "redstone_wire"},
        {BlockIds::REDSTONE_TORCH, "redstone_torch"},
        {BlockIds::REDSTONE_LAMP, "redstone_lamp"},
        {BlockIds::LEVER, "lever"},
        {BlockIds::STONE_BUTTON, "stone_button"},
        {BlockIds::OAK_BUTTON, "oak_button"},
        {BlockIds::STONE_PRESSURE_PLATE, "stone_pressure_plate"},
        {BlockIds::OAK_PRESSURE_PLATE, "oak_pressure_plate"},
        {BlockIds::REPEATER, "repeater"},
        {BlockIds::COMPARATOR, "comparator"},
        {BlockIds::PISTON, "piston"},
        {BlockIds::STICKY_PISTON, "sticky_piston"},
        {BlockIds::PISTON_HEAD, "piston_head"},
        {BlockIds::OBSERVER, "observer"},
        {BlockIds::DISPENSER, "dispenser"},
        {BlockIds::DROPPER, "dropper"},
        {BlockIds::HOPPER, "hopper"},
        {BlockIds::NOTE_BLOCK, "note_block"},
        {BlockIds::TARGET, "target"},
    };
    for (const auto& [blockId, name] : redstoneBlocks) {
        if (blockId == BlockIds::AIR) {
            std::cerr << "[block_state_registry_test] missing redstone block: " << name << '\n';
            return fail("redstone blocks should be registered from blocks.json");
        }
    }

    const auto modelVariantMatches = [](const StateID state,
                                        const char* expectedModel,
                                        const uint16_t expectedRotY,
                                        const uint16_t expectedRotX) {
        const ModelVariant* variant = BlockStateRegistry::getModelVariant(state);
        return variant != nullptr &&
               variant->model != nullptr &&
               variant->model->name == expectedModel &&
               variant->transform.rotY == expectedRotY &&
               variant->transform.rotX == expectedRotX;
    };
    const auto modelFaceTextureMatches = [](const BlockModel* model,
                                            const size_t faceIndex,
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
    const auto modelFaceRotationMatches = [](const BlockModel* model,
                                             const size_t faceIndex,
                                             const uint16_t expectedRotation) {
        if (model == nullptr || model->elements.empty() || faceIndex >= model->elements.front().faces.size()) {
            return false;
        }
        const std::unique_ptr<ModelFace>& face = model->elements.front().faces[faceIndex];
        return face != nullptr && face->uvRotation == expectedRotation;
    };

    const std::vector<BlockID> redstoneModelBlocks = {
        BlockIds::LEVER,
        BlockIds::STONE_BUTTON,
        BlockIds::OAK_BUTTON,
        BlockIds::STONE_PRESSURE_PLATE,
        BlockIds::OAK_PRESSURE_PLATE,
        BlockIds::REPEATER,
        BlockIds::COMPARATOR,
        BlockIds::PISTON,
        BlockIds::STICKY_PISTON,
        BlockIds::PISTON_HEAD,
        BlockIds::OBSERVER,
        BlockIds::DISPENSER,
        BlockIds::DROPPER,
        BlockIds::HOPPER,
    };
    for (const BlockID blockId : redstoneModelBlocks) {
        for (const StateID state : BlockStateRegistry::getStatesForBlock(blockId)) {
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
               modelFaceRotationMatches(model, modelFaceUp, 180) &&
               modelFaceRotationMatches(model, modelFaceDown, 0) &&
               modelFaceRotationMatches(model, modelFaceWest, 90) &&
               modelFaceRotationMatches(model, modelFaceEast, 270);
    };
    if (!pistonBodyFacesMatch("block/piston", "piston_top") ||
        !pistonBodyFacesMatch("block/sticky_piston", "piston_top_sticky") ||
        !pistonBodyFacesMatch("block/piston_extended", "piston_inner") ||
        !pistonBodyFacesMatch("block/sticky_piston_extended", "piston_inner")) {
        return fail("piston body models should orient side textures toward the local front face");
    }

    const BlockDef& redstoneWireDef = BlockRegistry::get(BlockIds::REDSTONE_WIRE);
    if (redstoneWireDef.renderShapeName != "redstone_wire" ||
        redstoneWireDef.renderShapeTag != MeshBuilderRegistry::REDSTONE_WIRE_TAG ||
        redstoneWireDef.placementStrategy != "face_plane_floor" ||
        redstoneWireDef.supportRule != "attached_face") {
        return fail("redstone_wire should use the custom wire render shape and floor support path");
    }
    const StateID redstoneWireDefault = BlockStateRegistry::getDefaultState(BlockIds::REDSTONE_WIRE);
    if (BlockStateRegistry::getPropertyIndex(redstoneWireDefault, PropIndices::FACING) != PropIndices::FACING_FLOOR ||
        BlockStateRegistry::getPropertyIndex(redstoneWireDefault, PropIndices::POWER) != PropIndices::POWER_0 ||
        BlockStateRegistry::getPropertyIndex(redstoneWireDefault, PropIndices::NORTH) != PropIndices::NORTH_FALSE ||
        BlockStateRegistry::getPropertyIndex(redstoneWireDefault, PropIndices::SOUTH) != PropIndices::SOUTH_FALSE ||
        BlockStateRegistry::getPropertyIndex(redstoneWireDefault, PropIndices::EAST) != PropIndices::EAST_FALSE ||
        BlockStateRegistry::getPropertyIndex(redstoneWireDefault, PropIndices::WEST) != PropIndices::WEST_FALSE) {
        return fail("redstone_wire default state should be floor power 0 with no visual connections");
    }
    const StateID redstoneWirePower15 = BlockStateRegistry::withProperty(
        redstoneWireDefault,
        PropIndices::POWER,
        PropIndices::POWER_15);
    if (redstoneWirePower15 == redstoneWireDefault ||
        BlockStateRegistry::getPropertyIndex(redstoneWirePower15, PropIndices::POWER) != PropIndices::POWER_15) {
        return fail("redstone_wire should expose power 15 state");
    }
    const StateID redstoneWireEast = BlockStateRegistry::withProperty(
        redstoneWireDefault,
        PropIndices::EAST,
        PropIndices::EAST_TRUE);
    if (redstoneWireEast == redstoneWireDefault ||
        BlockStateRegistry::getPropertyIndex(redstoneWireEast, PropIndices::EAST) != PropIndices::EAST_TRUE) {
        return fail("redstone_wire should expose directional visual connection states");
    }

    const BlockDef& redstoneTorchDef = BlockRegistry::get(BlockIds::REDSTONE_TORCH);
    if (redstoneTorchDef.renderShapeName != "torch" ||
        redstoneTorchDef.placementStrategy != "attach_wall" ||
        redstoneTorchDef.supportRule != "attached_face") {
        return fail("redstone_torch should use torch rendering and attach-wall placement");
    }
    const StateID redstoneTorchDefault = BlockStateRegistry::getDefaultState(BlockIds::REDSTONE_TORCH);
    if (BlockStateRegistry::getPropertyIndex(redstoneTorchDefault, PropIndices::FACING) != PropIndices::FACING_FLOOR ||
        BlockStateRegistry::getPropertyIndex(redstoneTorchDefault, PropIndices::LIT) != PropIndices::LIT_TRUE) {
        return fail("redstone_torch default state should be floor lit true");
    }

    const StateID redstoneLampLit = BlockStateRegistry::getState(
        BlockIds::REDSTONE_LAMP,
        PropIndices::LIT,
        PropIndices::LIT_TRUE);
    if (redstoneLampLit == BlockStateRegistry::getDefaultState(BlockIds::REDSTONE_LAMP) ||
        BlockStateRegistry::getPropertyIndex(redstoneLampLit, PropIndices::LIT) != PropIndices::LIT_TRUE) {
        return fail("redstone_lamp should expose lit and unlit display states");
    }

    const BlockDef& leverDef = BlockRegistry::get(BlockIds::LEVER);
    if (leverDef.renderShapeName != "model" ||
        leverDef.placementStrategy != "attach_wall" ||
        leverDef.supportRule != "attached_face") {
        return fail("lever should use model rendering and attach-wall placement");
    }
    PlacementStrategyFn leverStrategy = PlacementStrategyRegistry::getStrategy(leverDef.placementStrategy);
    if (leverStrategy == nullptr) {
        return fail("lever placement strategy should be registered");
    }
    PlacementContext leverWallPlacement;
    leverWallPlacement.blockId = BlockIds::LEVER;
    leverWallPlacement.hitNormal = {0, 0, -1};
    const StateID leverPlacedNorth = leverStrategy(leverWallPlacement);
    if (BlockStateRegistry::getPropertyIndex(leverPlacedNorth, PropIndices::FACING) != PropIndices::FACING_NORTH) {
        return fail("lever wall placement should derive outward facing from hit normal");
    }
    PlacementContext leverFloorPlacement;
    leverFloorPlacement.blockId = BlockIds::LEVER;
    leverFloorPlacement.hitNormal = {0, 1, 0};
    const StateID leverPlacedFloor = leverStrategy(leverFloorPlacement);
    if (BlockStateRegistry::getPropertyIndex(leverPlacedFloor, PropIndices::FACING) != PropIndices::FACING_FLOOR) {
        return fail("lever top-face placement should use the floor-facing state");
    }

    const StateID leverNorthUnpowered = BlockStateRegistry::getState(
        BlockIds::LEVER,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, PropIndices::FACING_NORTH},
            {PropIndices::POWERED, PropIndices::POWERED_FALSE}
        });
    if (!modelVariantMatches(leverNorthUnpowered, "block/lever_wall", 180, 0)) {
        return fail("lever north unpowered state should face out from the supporting wall");
    }
    const StateID leverSouthPowered = BlockStateRegistry::getState(
        BlockIds::LEVER,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, PropIndices::FACING_SOUTH},
            {PropIndices::POWERED, PropIndices::POWERED_TRUE}
        });
    if (!modelVariantMatches(leverSouthPowered, "block/lever_wall_powered", 0, 0)) {
        return fail("lever south powered state should use the unrotated powered wall model");
    }
    const StateID leverEastPowered = BlockStateRegistry::getState(
        BlockIds::LEVER,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, PropIndices::FACING_EAST},
            {PropIndices::POWERED, PropIndices::POWERED_TRUE}
        });
    if (!modelVariantMatches(leverEastPowered, "block/lever_wall_powered", 270, 0)) {
        return fail("lever east powered state should rotate the powered wall model outward");
    }
    const StateID leverWestPowered = BlockStateRegistry::getState(
        BlockIds::LEVER,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, PropIndices::FACING_WEST},
            {PropIndices::POWERED, PropIndices::POWERED_TRUE}
        });
    if (!modelVariantMatches(leverWestPowered, "block/lever_wall_powered", 90, 0)) {
        return fail("lever west powered state should resolve to the rotated powered wall model");
    }

    const StateID stoneButtonEastPressed = BlockStateRegistry::getState(
        BlockIds::STONE_BUTTON,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, PropIndices::FACING_EAST},
            {PropIndices::POWERED, PropIndices::POWERED_TRUE}
        });
    if (!modelVariantMatches(stoneButtonEastPressed, "block/stone_button_wall_pressed", 90, 0)) {
        return fail("stone_button east powered state should resolve to the rotated pressed wall model");
    }

    const StateID oakPressurePlatePressed = BlockStateRegistry::getState(
        BlockIds::OAK_PRESSURE_PLATE,
        PropIndices::POWERED,
        PropIndices::POWERED_TRUE);
    if (!modelVariantMatches(oakPressurePlatePressed, "block/oak_pressure_plate_pressed", 0, 0)) {
        return fail("oak_pressure_plate powered state should resolve to the pressed model");
    }

    const StateID repeaterDefault = BlockStateRegistry::getDefaultState(BlockIds::REPEATER);
    if (BlockStateRegistry::getPropertyIndex(repeaterDefault, PropIndices::DELAY) != PropIndices::DELAY_1) {
        return fail("repeater default state should use delay 1");
    }
    const StateID repeaterEastPowered = BlockStateRegistry::getState(
        BlockIds::REPEATER,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, PropIndices::FACING_EAST},
            {PropIndices::POWERED, PropIndices::POWERED_TRUE}
        });
    if (!modelVariantMatches(repeaterEastPowered, "block/repeater_1tick_on", 270, 0)) {
        return fail("repeater east powered default-delay state should resolve to the rotated 1-tick powered model");
    }
    const StateID repeaterEastDelay4Powered = BlockStateRegistry::getState(
        BlockIds::REPEATER,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, PropIndices::FACING_EAST},
            {PropIndices::POWERED, PropIndices::POWERED_TRUE},
            {PropIndices::DELAY, PropIndices::DELAY_4}
        });
    if (!modelVariantMatches(repeaterEastDelay4Powered, "block/repeater_4tick_on", 270, 0)) {
        return fail("repeater delay 4 powered state should resolve to the rotated 4-tick powered model");
    }

    const StateID comparatorDefault = BlockStateRegistry::getDefaultState(BlockIds::COMPARATOR);
    if (BlockStateRegistry::getPropertyIndex(comparatorDefault, PropIndices::MODE) != PropIndices::MODE_COMPARE) {
        return fail("comparator default state should use compare mode");
    }
    const StateID comparatorWestPowered = BlockStateRegistry::getState(
        BlockIds::COMPARATOR,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, PropIndices::FACING_WEST},
            {PropIndices::POWERED, PropIndices::POWERED_TRUE}
        });
    if (!modelVariantMatches(comparatorWestPowered, "block/comparator_powered", 90, 0)) {
        return fail("comparator west powered state should resolve to the rotated powered model");
    }
    const StateID comparatorWestSubtractPowered = BlockStateRegistry::getState(
        BlockIds::COMPARATOR,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, PropIndices::FACING_WEST},
            {PropIndices::POWERED, PropIndices::POWERED_TRUE},
            {PropIndices::MODE, PropIndices::MODE_SUBTRACT}
        });
    if (!modelVariantMatches(comparatorWestSubtractPowered, "block/comparator_subtract_powered", 90, 0)) {
        return fail("comparator subtract powered state should resolve to the rotated subtract powered model");
    }

    const StateID pistonExtendedNorth = BlockStateRegistry::getState(
        BlockIds::PISTON,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, PropIndices::FACING_NORTH},
            {PropIndices::EXTENDED, PropIndices::EXTENDED_TRUE}
        });
    if (!modelVariantMatches(pistonExtendedNorth, "block/piston_extended", 180, 0)) {
        return fail("piston extended north state should resolve to the extended piston model");
    }

    const StateID stickyPistonUp = BlockStateRegistry::getState(
        BlockIds::STICKY_PISTON,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, PropIndices::FACING_UP},
            {PropIndices::EXTENDED, PropIndices::EXTENDED_FALSE}
        });
    if (!modelVariantMatches(stickyPistonUp, "block/sticky_piston", 0, 270)) {
        return fail("sticky_piston up state should resolve to the rotated sticky piston model");
    }

    const StateID stickyPistonHeadEast = BlockStateRegistry::getState(
        BlockIds::PISTON_HEAD,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, PropIndices::FACING_EAST},
            {PropIndices::TYPE, PropIndices::TYPE_STICKY}
        });
    if (!modelVariantMatches(stickyPistonHeadEast, "block/sticky_piston_head", 270, 0)) {
        return fail("sticky piston head east state should resolve to the sticky head model");
    }

    const StateID observerDownPowered = BlockStateRegistry::getState(
        BlockIds::OBSERVER,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, PropIndices::FACING_DOWN},
            {PropIndices::POWERED, PropIndices::POWERED_TRUE}
        });
    if (!modelVariantMatches(observerDownPowered, "block/observer_powered", 0, 90)) {
        return fail("observer down powered state should resolve to the powered observer model");
    }

    const StateID dispenserUp = BlockStateRegistry::getState(
        BlockIds::DISPENSER,
        PropIndices::FACING,
        PropIndices::FACING_UP);
    if (!modelVariantMatches(dispenserUp, "block/dispenser", 0, 270)) {
        return fail("dispenser up state should resolve to the rotated dispenser model");
    }

    const StateID dropperEast = BlockStateRegistry::getState(
        BlockIds::DROPPER,
        PropIndices::FACING,
        PropIndices::FACING_EAST);
    if (!modelVariantMatches(dropperEast, "block/dropper", 270, 0)) {
        return fail("dropper east state should resolve to the rotated dropper model");
    }

    const StateID hopperSouth = BlockStateRegistry::getState(
        BlockIds::HOPPER,
        PropIndices::FACING,
        PropIndices::FACING_SOUTH);
    if (!modelVariantMatches(hopperSouth, "block/hopper_side", 180, 0)) {
        return fail("hopper south state should resolve to the rotated side hopper model");
    }

    const BlockDef& pistonDef = BlockRegistry::get(BlockIds::PISTON);
    const BlockDef& stickyPistonDef = BlockRegistry::get(BlockIds::STICKY_PISTON);
    if (!pistonDef.revertPlacementFacing || !stickyPistonDef.revertPlacementFacing) {
        return fail("pistons should parse placement facing revert from blocks.json");
    }
    PlacementStrategyFn pistonStrategy = PlacementStrategyRegistry::getStrategy(pistonDef.placementStrategy);
    if (pistonStrategy == nullptr) {
        return fail("six_way_facing placement strategy should be registered");
    }
    PlacementContext pistonPlacement;
    pistonPlacement.blockId = BlockIds::PISTON;
    pistonPlacement.hitNormal = glm::ivec3(0, 1, 0);
    const StateID pistonPlacedUp = pistonStrategy(pistonPlacement);
    if (BlockStateRegistry::getPropertyIndex(pistonPlacedUp, PropIndices::FACING) != PropIndices::FACING_UP) {
        return fail("six_way_facing placement should derive upward piston facing from floor hits");
    }
    pistonPlacement.hitNormal = glm::ivec3(0, -1, 0);
    const StateID pistonPlacedDown = pistonStrategy(pistonPlacement);
    if (BlockStateRegistry::getPropertyIndex(pistonPlacedDown, PropIndices::FACING) != PropIndices::FACING_DOWN) {
        return fail("six_way_facing placement should derive downward piston facing from ceiling hits");
    }
    pistonPlacement.hitNormal = glm::ivec3(1, 0, 0);
    pistonPlacement.playerYaw = 0.0f;
    const StateID pistonPlacedHorizontal = pistonStrategy(pistonPlacement);
    if (BlockStateRegistry::getPropertyIndex(pistonPlacedHorizontal, PropIndices::FACING) != PropIndices::FACING_WEST) {
        return fail("six_way_facing placement should apply the configured piston facing revert");
    }

    const BlockDef& hopperDef = BlockRegistry::get(BlockIds::HOPPER);
    PlacementStrategyFn hopperStrategy = PlacementStrategyRegistry::getStrategy(hopperDef.placementStrategy);
    if (hopperStrategy == nullptr) {
        return fail("hopper_facing placement strategy should be registered");
    }
    PlacementContext hopperPlacement;
    hopperPlacement.blockId = BlockIds::HOPPER;
    hopperPlacement.hitNormal = glm::ivec3(0, 1, 0);
    const StateID hopperPlacedDown = hopperStrategy(hopperPlacement);
    if (BlockStateRegistry::getPropertyIndex(hopperPlacedDown, PropIndices::FACING) != PropIndices::FACING_DOWN) {
        return fail("hopper floor placement should produce facing=down");
    }
    hopperPlacement.hitNormal = glm::ivec3(1, 0, 0);
    const StateID hopperPlacedWest = hopperStrategy(hopperPlacement);
    if (BlockStateRegistry::getPropertyIndex(hopperPlacedWest, PropIndices::FACING) != PropIndices::FACING_WEST) {
        return fail("hopper side placement should point into the clicked side");
    }

    if (BlockStateRegistry::getStateCount() <= BlockRegistry::getBlockCount()) {
        return fail("state registry should contain expanded states beyond raw block ids");
    }

    const StateID waterDefault = BlockStateRegistry::getDefaultState(BlockIds::WATER);
    if (!FluidState::isWater(waterDefault) || waterDefault == BlockIds::WATER) {
        return fail("water should expand into dedicated state ids");
    }
    if (!FluidState::isSource(waterDefault) || FluidState::level(waterDefault) != 0 || FluidState::isFalling(waterDefault)) {
        return fail("default water state should decode as a non-falling source");
    }

    const StateID waterLevel3 = FluidState::makeWater(3, false);
    if (FluidState::level(waterLevel3) != 3 || FluidState::isFalling(waterLevel3)) {
        return fail("water helper should build horizontal decay states");
    }

    const StateTextureIndices& waterDefaultTextures = BlockStateRegistry::getStateTextures(waterDefault);
    if (!waterDefaultTextures.faceTop.isAnimated ||
        waterDefaultTextures.faceTop.frameCount != 32 ||
        waterDefaultTextures.faceTop.fps <= 0.0f) {
        return fail("default water state should expose animated still top-face metadata");
    }
    if (!waterDefaultTextures.faceBottom.isAnimated ||
        waterDefaultTextures.faceBottom.frameCount != 32) {
        return fail("default water state should expose animated still bottom-face metadata");
    }
    if (!waterDefaultTextures.faceFront.isAnimated ||
        waterDefaultTextures.faceFront.frameCount != 32 ||
        waterDefaultTextures.faceFront.fps != waterDefaultTextures.faceTop.fps) {
        return fail("default water state sides should expose animated still metadata");
    }

    const StateTextureIndices& waterLevel3Textures = BlockStateRegistry::getStateTextures(waterLevel3);
    if (!waterLevel3Textures.faceTop.isAnimated ||
        waterLevel3Textures.faceTop.frameCount != 32 ||
        waterLevel3Textures.faceTop.fps != waterDefaultTextures.faceTop.fps) {
        return fail("flowing water top face should stay on the still animation");
    }

    const StateID fallingWater = FluidState::makeWater(0, true);
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
    if (waterDesc.blockId != BlockIds::WATER || waterDesc.tickDelay != 5 || waterDesc.maxLevel != 7) {
        return fail("fluid registry should load the configured water descriptor");
    }
    if (waterDesc.slopeSearchDistance != 5 || !waterDesc.canCreateInfiniteSource ||
        waterDesc.infiniteSourceNeighborCount != 2 || !waterDesc.requiresSupportForInfiniteSource) {
        return fail("fluid registry should expose water spread and infinite source settings");
    }

    const DecodedFluid decodedLevel3 = FluidState::decode(waterLevel3);
    if (decodedLevel3.kind != FluidKind::Water || decodedLevel3.level != 3 ||
        decodedLevel3.falling || decodedLevel3.isSource) {
        return fail("fluid decode should preserve water level metadata");
    }
    if (FluidState::encode(decodedLevel3) != waterLevel3) {
        return fail("fluid encode should round-trip decoded water states");
    }
    if (!FluidState::canReplace(waterDesc, BlockIds::AIR) || !FluidState::canReplace(waterDesc, waterLevel3)) {
        return fail("water should be allowed to replace air and existing water");
    }
    if (FluidState::canReplace(waterDesc, BlockIds::STONE) || FluidState::canCoexist(waterDesc, BlockIds::STONE)) {
        return fail("waterlogging placeholder APIs should not allow solid coexistence yet");
    }

    {
        SubChunk subChunk;
        std::vector<BlockID> ids;
        ids.reserve(300);
        for (int i = 0; i < 300; ++i) {
            const BlockID id = BlockRegistry::registerBlock(
                NamespacedId("test", "palette_" + std::to_string(i)),
                BlockDef{});
            ids.push_back(id);

            const int x = i % SubChunk::SIZE;
            const int z = (i / SubChunk::SIZE) % SubChunk::SIZE;
            const int y = i / (SubChunk::SIZE * SubChunk::SIZE);
            subChunk.setBlockFast(x, y, z, id);
        }

        for (int i = 0; i < 300; ++i) {
            const int x = i % SubChunk::SIZE;
            const int z = (i / SubChunk::SIZE) % SubChunk::SIZE;
            const int y = i / (SubChunk::SIZE * SubChunk::SIZE);
            if (subChunk.getBlock(x, y, z) != ids[i]) {
                return fail("sub-chunk palette should preserve block ids past 256 unique entries");
            }
        }
    }

    std::cout << "[block_state_registry_test] PASS\n";
    return EXIT_SUCCESS;
}
