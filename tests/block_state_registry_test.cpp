#include <cstdlib>
#include <iostream>
#include <string>
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

    const std::string torchStateString = BlockStateRegistry::stateToString(torchNorth);
    if (torchStateString.find("facing=north") == std::string::npos) {
        return fail("stateToString should include the resolved torch facing");
    }

    const BlockDef& torchDef = BlockRegistry::get(BlockIds::TORCH);
    if (torchDef.placementStrategy != "attach_wall") {
        return fail("torch should parse placementStrategy from blocks.json");
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
    const StateID oakStairsSouth = BlockStateRegistry::getState(
        oakStairs,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, PropIndices::FACING_SOUTH},
            {PropIndices::HALF, PropIndices::HALF_BOTTOM}
        });
    const ModelVariant* oakStairsVariant = BlockStateRegistry::getModelVariant(oakStairsSouth);
    if (oakStairsVariant == nullptr || oakStairsVariant->model == nullptr ||
        oakStairsVariant->model->name != "block/oak_stairs" ||
        oakStairsVariant->transform.rotY != 90) {
        return fail("oak_stairs south state should resolve to the rotated oak stairs model");
    }

    const BlockID oakSlab = BlockRegistry::findByName("oak_slab");
    if (oakSlab == BlockIds::AIR) {
        return fail("oak_slab should be registered from blocks.json");
    }
    const BlockDef& oakSlabDef = BlockRegistry::get(oakSlab);
    if (oakSlabDef.placementStrategy != "slab") {
        return fail("oak_slab should parse slab placement strategy");
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

    slabTopPlacement.isSneaking = true;
    const StateID slabSneakPlacedBottom = slabStrategy(slabTopPlacement);
    if (BlockStateRegistry::getPropertyIndex(slabSneakPlacedBottom, PropIndices::HALF) != PropIndices::HALF_BOTTOM) {
        return fail("sneaking slab placement should invert the selected half");
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
    if (chestDef.placementStrategy != "horizontal_facing") {
        return fail("chest should parse horizontal_facing placement strategy");
    }

    PlacementStrategyFn chestStrategy = PlacementStrategyRegistry::getStrategy(chestDef.placementStrategy);
    if (chestStrategy == nullptr) {
        return fail("chest placement strategy should be registered");
    }

    PlacementContext chestPlacement;
    chestPlacement.blockId = BlockIds::CHEST;
    chestPlacement.playerYaw = 180.0f;
    const StateID chestPlacedNorth = chestStrategy(chestPlacement);
    if (BlockStateRegistry::getPropertyIndex(chestPlacedNorth, PropIndices::FACING) != PropIndices::FACING_NORTH) {
        return fail("horizontal_facing placement should derive chest facing from player yaw");
    }

    PlacementStrategyFn anvilStrategy = PlacementStrategyRegistry::getStrategy(anvilDef.placementStrategy);
    if (anvilStrategy == nullptr) {
        return fail("anvil placement strategy should be registered");
    }

    PlacementContext anvilPlacement;
    anvilPlacement.blockId = anvil;
    anvilPlacement.playerYaw = 270.0f;
    const StateID anvilPlacedEast = anvilStrategy(anvilPlacement);
    if (BlockStateRegistry::getPropertyIndex(anvilPlacedEast, PropIndices::FACING) != PropIndices::FACING_EAST) {
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
