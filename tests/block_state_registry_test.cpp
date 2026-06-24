#include <cstdlib>
#include <iostream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "../src/world/block/Block.h"
#include "../src/world/block/BlockStateRegistry.h"
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
