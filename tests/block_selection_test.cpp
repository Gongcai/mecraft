#include <cstdlib>
#include <iostream>
#include <utility>
#include <vector>

#include <glm/vec3.hpp>

#include "../src/world/block/BedBlock.h"
#include "../src/world/block/BlockCollision.h"
#include "../src/world/block/BlockSelection.h"
#include "../src/world/block/PropIndices.h"
#include "../src/world/World.h"

namespace {

int fail(const char* message) {
    std::cerr << "[block_selection_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

void loadChunks(World& world) {
    world.setRenderDistance(1);
    for (int i = 0; i < 8; ++i) {
        world.update(glm::vec3(0.0f));
    }
}

} // namespace

int main() {
    BlockRegistry::init(nullptr);

    const StateID floorTorch = BlockStateRegistry::getDefaultState(BlockIds::TORCH);
    const BlockSelectionBox floorBox = BlockSelection::getBox(floorTorch);
    if (floorBox.min.x >= 0.45f || floorBox.max.x <= 0.55f || floorBox.max.y >= 0.7f) {
        return fail("floor torch selection box should be a short narrow core");
    }

    const StateID northTorch = BlockStateRegistry::getState(
        BlockIds::TORCH,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, PropIndices::FACING_NORTH}
        });
    const BlockSelectionBox northBox = BlockSelection::getBox(northTorch);
    if (northBox.min.z <= 0.45f || northBox.max.y < 0.7f) {
        return fail("north wall torch selection box should follow the tilted wall model");
    }

    const StateID southTorch = BlockStateRegistry::getState(
        BlockIds::TORCH,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, PropIndices::FACING_SOUTH}
        });
    const BlockSelectionBox southBox = BlockSelection::getBox(southTorch);
    if (southBox.max.z >= 0.55f || southBox.max.y < 0.7f) {
        return fail("south wall torch selection box should mirror the north wall model");
    }

    const StateID eastTorch = BlockStateRegistry::getState(
        BlockIds::TORCH,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, PropIndices::FACING_EAST}
        });
    const BlockSelectionBox eastBox = BlockSelection::getBox(eastTorch);
    if (eastBox.max.x >= 0.55f || eastBox.max.y < 0.7f) {
        return fail("east wall torch selection box should rotate onto the X axis");
    }

    const StateID westTorch = BlockStateRegistry::getState(
        BlockIds::TORCH,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, PropIndices::FACING_WEST}
        });
    const BlockSelectionBox westBox = BlockSelection::getBox(westTorch);
    if (westBox.min.x <= 0.45f || westBox.max.y < 0.7f) {
        return fail("west wall torch selection box should mirror the east wall model");
    }

    const BlockSelectionBox crossBox = BlockSelection::getBox(BlockIds::TALL_GRASS);
    if (crossBox.min.x <= 0.1f || crossBox.max.x >= 0.9f ||
        crossBox.min.z <= 0.1f || crossBox.max.z >= 0.9f) {
        return fail("cross-shaped plants should not use a full horizontal voxel selection box");
    }

    const BlockID oakSlab = BlockRegistry::findByName("oak_slab");
    if (oakSlab == BlockIds::AIR) {
        return fail("oak_slab should be registered for selection tests");
    }
    const StateID bottomSlab = BlockStateRegistry::getDefaultState(oakSlab);
    const BlockSelectionBox bottomSlabBox = BlockSelection::getBox(bottomSlab);
    if (bottomSlabBox.min.y != 0.0f || bottomSlabBox.max.y != 0.5f) {
        return fail("bottom slab selection box should cover the lower half");
    }
    const StateID topSlab = BlockStateRegistry::getState(
        oakSlab,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::HALF, PropIndices::HALF_TOP}
        });
    const BlockSelectionBox topSlabBox = BlockSelection::getBox(topSlab);
    if (topSlabBox.min.y != 0.5f || topSlabBox.max.y != 1.0f) {
        return fail("top slab selection box should cover the upper half");
    }
    const StateID doubleSlab = BlockStateRegistry::getState(
        oakSlab,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::HALF, PropIndices::HALF_DOUBLE}
        });
    const BlockSelectionBox doubleSlabBox = BlockSelection::getBox(doubleSlab);
    if (doubleSlabBox.min.y != 0.0f || doubleSlabBox.max.y != 1.0f) {
        return fail("double slab selection box should cover the full block");
    }
    const std::vector<BlockCollisionBox> bottomSlabCollision = BlockCollision::getBoxes(bottomSlab);
    if (bottomSlabCollision.size() != 1 ||
        bottomSlabCollision.front().min.y != 0.0f ||
        bottomSlabCollision.front().max.y != 0.5f) {
        return fail("bottom slab collision should use the lower model element");
    }
    const std::vector<BlockCollisionBox> topSlabCollision = BlockCollision::getBoxes(topSlab);
    if (topSlabCollision.size() != 1 ||
        topSlabCollision.front().min.y != 0.5f ||
        topSlabCollision.front().max.y != 1.0f) {
        return fail("top slab collision should use the upper model element");
    }
    const std::vector<BlockCollisionBox> doubleSlabCollision = BlockCollision::getBoxes(doubleSlab);
    if (doubleSlabCollision.size() != 1 ||
        doubleSlabCollision.front().min.y != 0.0f ||
        doubleSlabCollision.front().max.y != 1.0f) {
        return fail("double slab collision should cover the full block");
    }

    const BlockID oakVerticalSlab = BlockRegistry::findByName("oak_vertical_slab");
    if (oakVerticalSlab == BlockIds::AIR) {
        return fail("oak_vertical_slab should be registered for selection tests");
    }
    const StateID eastVerticalSlab = BlockStateRegistry::getState(
        oakVerticalSlab,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::HALF, PropIndices::HALF_EAST}
        });
    const BlockSelectionBox eastVerticalSlabBox = BlockSelection::getBox(eastVerticalSlab);
    if (eastVerticalSlabBox.min.x != 0.5f || eastVerticalSlabBox.max.x != 1.0f ||
        eastVerticalSlabBox.min.y != 0.0f || eastVerticalSlabBox.max.y != 1.0f ||
        eastVerticalSlabBox.min.z != 0.0f || eastVerticalSlabBox.max.z != 1.0f) {
        return fail("east vertical slab selection box should cover the eastern half");
    }
    const StateID northVerticalSlab = BlockStateRegistry::getState(
        oakVerticalSlab,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::HALF, PropIndices::HALF_NORTH}
        });
    const std::vector<BlockCollisionBox> northVerticalSlabCollision = BlockCollision::getBoxes(northVerticalSlab);
    if (northVerticalSlabCollision.size() != 1 ||
        northVerticalSlabCollision.front().min.z != 0.0f ||
        northVerticalSlabCollision.front().max.z != 0.5f ||
        northVerticalSlabCollision.front().max.y != 1.0f) {
        return fail("north vertical slab collision should cover the northern half");
    }

    const BlockID vine = BlockRegistry::findByName("vine");
    if (vine == BlockIds::AIR) {
        return fail("vine should be registered for face plane selection tests");
    }
    const StateID northVine = BlockStateRegistry::getState(
        vine,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, PropIndices::FACING_NORTH}
        });
    const BlockSelectionBox northVineBox = BlockSelection::getBox(northVine);
    if (northVineBox.min.z != 0.9375f || northVineBox.max.z != 1.0f ||
        northVineBox.min.x != 0.0f || northVineBox.max.x != 1.0f ||
        northVineBox.min.y != 0.0f || northVineBox.max.y != 1.0f) {
        return fail("north-facing vine selection should hug the attached wall boundary");
    }
    const StateID eastVine = BlockStateRegistry::getState(
        vine,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, PropIndices::FACING_EAST}
        });
    const BlockSelectionBox eastVineBox = BlockSelection::getBox(eastVine);
    if (eastVineBox.min.x != 0.0f || eastVineBox.max.x != 0.0625f ||
        eastVineBox.min.z != 0.0f || eastVineBox.max.z != 1.0f) {
        return fail("east-facing vine selection should hug the attached wall boundary");
    }
    if (!BlockCollision::getBoxes(northVine).empty() ||
        BlockCollision::intersects(northVine, glm::ivec3(0), glm::vec3(0.0f), glm::vec3(1.0f))) {
        return fail("face plane blocks should not create movement collision");
    }

    const StateID redstoneWireDefault = BlockStateRegistry::getDefaultState(BlockIds::REDSTONE_WIRE);
    const BlockSelectionBox redstoneWireBox = BlockSelection::getBox(redstoneWireDefault);
    if (redstoneWireBox.min.y != 0.0f || redstoneWireBox.max.y != 0.0625f ||
        redstoneWireBox.min.x != 0.0f || redstoneWireBox.max.x != 1.0f ||
        redstoneWireBox.min.z != 0.0f || redstoneWireBox.max.z != 1.0f) {
        return fail("redstone_wire selection should stay on the floor face plane");
    }
    if (!BlockCollision::getBoxes(redstoneWireDefault).empty()) {
        return fail("redstone_wire should not create movement collision");
    }

    const BlockID pinkPetals = BlockRegistry::findByName("pink_petals");
    if (pinkPetals == BlockIds::AIR) {
        return fail("pink_petals should be registered for face plane selection tests");
    }
    const StateID floorPetals = BlockStateRegistry::getDefaultState(pinkPetals);
    const BlockSelectionBox floorPetalBox = BlockSelection::getBox(floorPetals);
    if (floorPetalBox.min.y != 0.0f || floorPetalBox.max.y != 0.0625f ||
        floorPetalBox.min.x != 0.0f || floorPetalBox.max.x != 1.0f ||
        floorPetalBox.min.z != 0.0f || floorPetalBox.max.z != 1.0f) {
        return fail("floor face plane selection should hug the ground boundary");
    }
    if (!BlockCollision::getBoxes(floorPetals).empty()) {
        return fail("floor face plane blocks should not create movement collision");
    }

    const BlockID oakStairs = BlockRegistry::findByName("oak_stairs");
    if (oakStairs == BlockIds::AIR) {
        return fail("oak_stairs should be registered for collision tests");
    }
    const StateID defaultStairs = BlockStateRegistry::getDefaultState(oakStairs);
    const std::vector<BlockCollisionBox> stairCollision = BlockCollision::getBoxes(defaultStairs);
    if (stairCollision.size() < 2) {
        return fail("stairs collision should keep separate model element boxes");
    }
    const StateID northBottomStairs = BlockStateRegistry::getState(
        oakStairs,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, PropIndices::FACING_NORTH},
            {PropIndices::HALF, PropIndices::HALF_BOTTOM},
            {PropIndices::SHAPE, PropIndices::SHAPE_STRAIGHT}
        });
    const std::vector<BlockCollisionBox> northStairCollision = BlockCollision::getBoxes(northBottomStairs);
    bool foundNorthUpperHalf = false;
    for (const BlockCollisionBox& box : northStairCollision) {
        if (box.min.x == 0.0f && box.max.x == 1.0f &&
            box.min.y == 0.5f && box.max.y == 1.0f &&
            box.min.z == 0.0f && box.max.z == 0.5f) {
            foundNorthUpperHalf = true;
        }
    }
    if (!foundNorthUpperHalf) {
        return fail("north-facing stairs collision should place the upper half on the north side");
    }

    const StateID oakDoorClosedEast = BlockStateRegistry::getDefaultState(BlockIds::OAK_DOOR);
    const BlockSelectionBox oakDoorClosedBox = BlockSelection::getBox(oakDoorClosedEast);
    if (oakDoorClosedBox.min.x != 0.0f || oakDoorClosedBox.max.x != 0.1875f ||
        oakDoorClosedBox.min.y != 0.0f || oakDoorClosedBox.max.y != 1.0f ||
        oakDoorClosedBox.min.z != 0.0f || oakDoorClosedBox.max.z != 1.0f) {
        return fail("closed east-facing oak_door should occupy the west-side door slab");
    }

    const StateID oakDoorOpenLeftEast = BlockStateRegistry::getState(
        BlockIds::OAK_DOOR,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, PropIndices::FACING_EAST},
            {PropIndices::HALF, PropIndices::HALF_LOWER},
            {PropIndices::HINGE, PropIndices::HINGE_LEFT},
            {PropIndices::OPEN, PropIndices::OPEN_TRUE},
            {PropIndices::POWERED, PropIndices::POWERED_FALSE}
        });
    const BlockSelectionBox oakDoorOpenLeftBox = BlockSelection::getBox(oakDoorOpenLeftEast);
    if (oakDoorOpenLeftBox.min.x != 0.0f || oakDoorOpenLeftBox.max.x != 1.0f ||
        oakDoorOpenLeftBox.min.y != 0.0f || oakDoorOpenLeftBox.max.y != 1.0f ||
        oakDoorOpenLeftBox.min.z != 0.0f || oakDoorOpenLeftBox.max.z != 0.1875f) {
        return fail("left-hinge open east-facing oak_door selection should rotate onto the north-side slab");
    }
    const std::vector<BlockCollisionBox> oakDoorOpenLeftCollision =
        BlockCollision::getBoxes(oakDoorOpenLeftEast);
    if (oakDoorOpenLeftCollision.size() != 1 ||
        oakDoorOpenLeftCollision.front().min.x != 0.0f ||
        oakDoorOpenLeftCollision.front().max.x != 1.0f ||
        oakDoorOpenLeftCollision.front().min.z != 0.0f ||
        oakDoorOpenLeftCollision.front().max.z != 0.1875f) {
        return fail("left-hinge open east-facing oak_door collision should follow the rotated model slab");
    }

    const StateID oakDoorOpenRightEast = BlockStateRegistry::getState(
        BlockIds::OAK_DOOR,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, PropIndices::FACING_EAST},
            {PropIndices::HALF, PropIndices::HALF_LOWER},
            {PropIndices::HINGE, PropIndices::HINGE_RIGHT},
            {PropIndices::OPEN, PropIndices::OPEN_TRUE},
            {PropIndices::POWERED, PropIndices::POWERED_FALSE}
        });
    const std::vector<BlockCollisionBox> oakDoorOpenRightCollision =
        BlockCollision::getBoxes(oakDoorOpenRightEast);
    if (oakDoorOpenRightCollision.size() != 1 ||
        oakDoorOpenRightCollision.front().min.x != 0.0f ||
        oakDoorOpenRightCollision.front().max.x != 1.0f ||
        oakDoorOpenRightCollision.front().min.z != 0.8125f ||
        oakDoorOpenRightCollision.front().max.z != 1.0f) {
        return fail("right-hinge open east-facing oak_door collision should rotate onto the south-side slab");
    }

    const BlockID cauldron = BlockRegistry::findByName("cauldron");
    if (cauldron == BlockIds::AIR) {
        return fail("cauldron should be registered for collision tests");
    }
    const StateID defaultCauldron = BlockStateRegistry::getDefaultState(cauldron);
    const BlockSelectionBox cauldronBox = BlockSelection::getBox(defaultCauldron);
    if (cauldronBox.min.y != 0.0f || cauldronBox.max.y != 1.0f ||
        cauldronBox.min.x != 0.0f || cauldronBox.max.x != 1.0f ||
        cauldronBox.min.z != 0.0f || cauldronBox.max.z != 1.0f) {
        return fail("cauldron selection box should cover the full model bounds");
    }
    const std::vector<BlockCollisionBox> cauldronCollision = BlockCollision::getBoxes(defaultCauldron);
    if (cauldronCollision.size() != 13) {
        return fail("cauldron collision should keep each model element box");
    }
    bool foundLeftWall = false;
    bool foundInnerFloor = false;
    for (const BlockCollisionBox& box : cauldronCollision) {
        if (box.min.x == 0.0f && box.max.x == 0.125f &&
            box.min.y == 0.1875f && box.max.y == 1.0f &&
            box.min.z == 0.0f && box.max.z == 1.0f) {
            foundLeftWall = true;
        }
        if (box.min.x == 0.125f && box.max.x == 0.875f &&
            box.min.y == 0.1875f && box.max.y == 0.25f &&
            box.min.z == 0.125f && box.max.z == 0.875f) {
            foundInnerFloor = true;
        }
    }
    if (!foundLeftWall || !foundInnerFloor) {
        return fail("cauldron collision should include wall and inner floor elements");
    }

    const BlockID oakFence = BlockRegistry::findByName("oak_fence");
    if (oakFence == BlockIds::AIR) {
        return fail("oak_fence should be registered for collision tests");
    }
    const StateID oakFenceEastWest = BlockStateRegistry::getState(
        oakFence,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::NORTH, PropIndices::NORTH_FALSE},
            {PropIndices::SOUTH, PropIndices::SOUTH_FALSE},
            {PropIndices::EAST, PropIndices::EAST_TRUE},
            {PropIndices::WEST, PropIndices::WEST_TRUE}
        });
    const std::vector<BlockCollisionBox> fenceCollision = BlockCollision::getBoxes(oakFenceEastWest);
    if (fenceCollision.size() != 5) {
        return fail("east/west oak fence collision should include post and four rail boxes");
    }
    const BlockSelectionBox fenceSelection = BlockSelection::getBox(oakFenceEastWest);
    if (fenceSelection.min.x != 0.0f || fenceSelection.max.x != 1.0f ||
        fenceSelection.min.z != 0.375f || fenceSelection.max.z != 0.625f ||
        fenceSelection.min.y != 0.0f || fenceSelection.max.y != 1.0f) {
        return fail("east/west oak fence selection should cover the connected rail span");
    }

    const BlockID cobblestoneWall = BlockRegistry::findByName("cobblestone_wall");
    if (cobblestoneWall == BlockIds::AIR) {
        return fail("cobblestone_wall should be registered for collision tests");
    }
    const StateID cobblestoneWallNorthSouth = BlockStateRegistry::getState(
        cobblestoneWall,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::NORTH, PropIndices::NORTH_TRUE},
            {PropIndices::SOUTH, PropIndices::SOUTH_TRUE},
            {PropIndices::EAST, PropIndices::EAST_FALSE},
            {PropIndices::WEST, PropIndices::WEST_FALSE}
        });
    const std::vector<BlockCollisionBox> wallCollision = BlockCollision::getBoxes(cobblestoneWallNorthSouth);
    if (wallCollision.size() != 3) {
        return fail("north/south cobblestone wall collision should include post and two wall arms");
    }
    const BlockSelectionBox wallSelection = BlockSelection::getBox(cobblestoneWallNorthSouth);
    if (wallSelection.min.x != 0.25f || wallSelection.max.x != 0.75f ||
        wallSelection.min.z != 0.0f || wallSelection.max.z != 1.0f ||
        wallSelection.min.y != 0.0f || wallSelection.max.y != 1.0f) {
        return fail("north/south cobblestone wall selection should cover the connected wall span");
    }

    const BlockID anvil = BlockRegistry::findByName("anvil");
    if (anvil == BlockIds::AIR) {
        return fail("anvil should be registered for collision tests");
    }
    const StateID anvilEast = BlockStateRegistry::getState(
        anvil,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, PropIndices::FACING_EAST}
        });
    const BlockSelectionBox anvilEastBox = BlockSelection::getBox(anvilEast);
    if (anvilEastBox.min.x != 0.0f || anvilEastBox.max.x != 1.0f ||
        anvilEastBox.min.z != 0.125f || anvilEastBox.max.z != 0.875f ||
        anvilEastBox.min.y != 0.0f || anvilEastBox.max.y != 1.0f) {
        return fail("east-facing anvil selection box should rotate the model bounds");
    }
    const std::vector<BlockCollisionBox> anvilCollision = BlockCollision::getBoxes(anvilEast);
    if (anvilCollision.size() != 4) {
        return fail("anvil collision should keep each model element box");
    }
    bool foundRotatedTop = false;
    for (const BlockCollisionBox& box : anvilCollision) {
        if (box.min.x == 0.0f && box.max.x == 1.0f &&
            box.min.y == 0.625f && box.max.y == 1.0f &&
            box.min.z == 0.1875f && box.max.z == 0.8125f) {
            foundRotatedTop = true;
        }
    }
    if (!foundRotatedTop) {
        return fail("east-facing anvil collision should include the rotated top element");
    }

    const BlockID redBed = BlockRegistry::findByName("red_bed");
    if (redBed == BlockIds::AIR) {
        return fail("red_bed should be registered for selection tests");
    }
    const StateID redBedFoot = BlockStateRegistry::getDefaultState(redBed);
    const BlockSelectionBox redBedBox = BlockSelection::getBox(redBedFoot);
    if (redBedBox.min.x != 0.0f || redBedBox.max.x != 1.0f ||
        redBedBox.min.y != 0.0f || redBedBox.max.y != 0.5625f ||
        redBedBox.min.z != 0.0f || redBedBox.max.z != 1.0f) {
        return fail("red_bed selection box should cover the low bed model bounds");
    }
    const std::vector<BlockCollisionBox> redBedCollision = BlockCollision::getBoxes(redBedFoot);
    if (redBedCollision.size() != 3) {
        return fail("red_bed collision should include the mattress and two legs");
    }
    bool foundBedBody = false;
    for (const BlockCollisionBox& box : redBedCollision) {
        if (box.min.x == 0.0f && box.max.x == 1.0f &&
            box.min.y == 0.1875f && box.max.y == 0.5625f &&
            box.min.z == 0.0f && box.max.z == 1.0f) {
            foundBedBody = true;
        }
    }
    if (!foundBedBody) {
        return fail("red_bed collision should include the mattress body");
    }

    const StateID poweredTarget = BlockStateRegistry::getState(
        BlockIds::TARGET,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::POWER, PropIndices::POWER_15}
        });
    const BlockSelectionBox poweredTargetBox = BlockSelection::getBox(poweredTarget);
    if (poweredTargetBox.min != glm::vec3(0.0f) || poweredTargetBox.max != glm::vec3(1.0f)) {
        return fail("powered target selection should keep the target block shape");
    }
    const std::vector<BlockCollisionBox> poweredTargetCollision = BlockCollision::getBoxes(poweredTarget);
    if (poweredTargetCollision.size() != 1 ||
        poweredTargetCollision.front().min != glm::vec3(0.0f) ||
        poweredTargetCollision.front().max != glm::vec3(1.0f) ||
        !BlockCollision::intersects(poweredTarget,
                                    glm::ivec3(0),
                                    glm::vec3(0.75f),
                                    glm::vec3(1.25f))) {
        return fail("powered target collision should resolve through its block definition");
    }

    World world;
    world.init(20260507);
    loadChunks(world);

    const int baseY = world.getSurfaceY(0, 0) + 2;
    world.setBlockState(12, baseY, 0, BlockIds::AIR);
    world.setBlockState(13, baseY, 0, BlockIds::AIR);
    const BedBlockLogic::BedPlacement bedPlacement =
        BedBlockLogic::resolvePlacement(world, glm::ivec3(12, baseY, 0), redBedFoot);
    if (!bedPlacement.valid ||
        bedPlacement.footPos != glm::ivec3(12, baseY, 0) ||
        bedPlacement.headPos != glm::ivec3(13, baseY, 0)) {
        return fail("east-facing red_bed placement should occupy foot and head cells");
    }
    BedBlockLogic::placeBed(world, bedPlacement);
    if (!BedBlockLogic::isFootState(world.getBlockState(12, baseY, 0)) ||
        !BedBlockLogic::isHeadState(world.getBlockState(13, baseY, 0))) {
        return fail("red_bed placement should write foot and head states");
    }
    std::vector<glm::ivec3> removedBedPositions;
    const BlockID removedBed = BedBlockLogic::removeBed(world, glm::ivec3(13, baseY, 0), &removedBedPositions);
    if (removedBed != redBed ||
        world.getBlockState(12, baseY, 0) != BlockIds::AIR ||
        world.getBlockState(13, baseY, 0) != BlockIds::AIR ||
        removedBedPositions.size() != 2) {
        return fail("breaking either red_bed half should remove both bed states");
    }

    const auto makeBottomStairs = [&](const uint16_t facing) {
        return BlockStateRegistry::getState(
            oakStairs,
            std::vector<std::pair<uint16_t, uint16_t>>{
                {PropIndices::FACING, facing},
                {PropIndices::HALF, PropIndices::HALF_BOTTOM},
                {PropIndices::SHAPE, PropIndices::SHAPE_STRAIGHT}
            });
    };

    world.setBlockState(2, baseY, 0, BlockIds::AIR);
    world.setBlockState(3, baseY, 0, BlockIds::AIR);
    world.setBlockState(2, baseY, 0, oakFence);
    StateID placedFence = world.getBlockState(2, baseY, 0);
    if (BlockStateRegistry::getPropertyIndex(placedFence, PropIndices::EAST) != PropIndices::EAST_FALSE) {
        return fail("isolated oak fence should start disconnected");
    }
    world.setBlockState(3, baseY, 0, BlockIds::STONE);
    placedFence = world.getBlockState(2, baseY, 0);
    if (BlockStateRegistry::getPropertyIndex(placedFence, PropIndices::EAST) != PropIndices::EAST_TRUE) {
        return fail("oak fence should connect to a solid east neighbor after placement");
    }
    world.setBlockState(3, baseY, 0, BlockIds::AIR);
    placedFence = world.getBlockState(2, baseY, 0);
    if (BlockStateRegistry::getPropertyIndex(placedFence, PropIndices::EAST) != PropIndices::EAST_FALSE) {
        return fail("oak fence should disconnect when the east neighbor is removed");
    }

    world.setBlockState(4, baseY, 0, BlockIds::AIR);
    world.setBlockState(4, baseY, -1, BlockIds::AIR);
    world.setBlockState(4, baseY, 0, cobblestoneWall);
    StateID placedWall = world.getBlockState(4, baseY, 0);
    if (BlockStateRegistry::getPropertyIndex(placedWall, PropIndices::NORTH) != PropIndices::NORTH_FALSE) {
        return fail("isolated cobblestone wall should start disconnected");
    }
    world.setBlockState(4, baseY, -1, oakFence);
    placedWall = world.getBlockState(4, baseY, 0);
    if (BlockStateRegistry::getPropertyIndex(placedWall, PropIndices::NORTH) != PropIndices::NORTH_TRUE) {
        return fail("cobblestone wall should connect to a north fence neighbor after placement");
    }
    world.setBlockState(4, baseY, -1, BlockIds::AIR);
    placedWall = world.getBlockState(4, baseY, 0);
    if (BlockStateRegistry::getPropertyIndex(placedWall, PropIndices::NORTH) != PropIndices::NORTH_FALSE) {
        return fail("cobblestone wall should disconnect when the north neighbor is removed");
    }

    const int wireY = baseY + 1;
    world.setBlockState(10, baseY, 2, BlockIds::STONE);
    world.setBlockState(11, baseY, 2, BlockIds::STONE);
    world.setBlockState(10, wireY, 2, BlockIds::AIR);
    world.setBlockState(11, wireY, 2, BlockIds::AIR);
    world.setBlockState(10, wireY, 2, BlockIds::REDSTONE_WIRE);
    StateID placedWire = world.getBlockState(10, wireY, 2);
    if (BlockStateRegistry::getPropertyIndex(placedWire, PropIndices::EAST) != PropIndices::EAST_NONE) {
        return fail("isolated redstone_wire should start visually disconnected");
    }
    world.setBlockState(11, wireY, 2, BlockIds::REDSTONE_WIRE);
    placedWire = world.getBlockState(10, wireY, 2);
    const StateID eastWire = world.getBlockState(11, wireY, 2);
    if (BlockStateRegistry::getPropertyIndex(placedWire, PropIndices::EAST) != PropIndices::EAST_SIDE ||
        BlockStateRegistry::getPropertyIndex(eastWire, PropIndices::WEST) != PropIndices::WEST_SIDE) {
        return fail("adjacent redstone_wire blocks should refresh visual connection states");
    }
    world.setBlockState(11, wireY, 2, BlockIds::AIR);
    placedWire = world.getBlockState(10, wireY, 2);
    if (BlockStateRegistry::getPropertyIndex(placedWire, PropIndices::EAST) != PropIndices::EAST_NONE) {
        return fail("redstone_wire should disconnect visually when its neighbor is removed");
    }

    world.setBlockState(6, baseY, 0, BlockIds::AIR);
    world.setBlockState(7, baseY, 0, BlockIds::AIR);
    world.setBlockState(6, baseY, 0, makeBottomStairs(PropIndices::FACING_EAST));
    StateID placedStairs = world.getBlockState(6, baseY, 0);
    if (BlockStateRegistry::getPropertyIndex(placedStairs, PropIndices::SHAPE) != PropIndices::SHAPE_STRAIGHT) {
        return fail("isolated stairs should keep shape=straight");
    }
    world.setBlockState(7, baseY, 0, makeBottomStairs(PropIndices::FACING_SOUTH));
    placedStairs = world.getBlockState(6, baseY, 0);
    if (BlockStateRegistry::getPropertyIndex(placedStairs, PropIndices::SHAPE) != PropIndices::SHAPE_OUTER_RIGHT) {
        return fail("front perpendicular stairs should update the source stair to an outer-right corner");
    }
    world.setBlockState(7, baseY, 0, BlockIds::AIR);
    placedStairs = world.getBlockState(6, baseY, 0);
    if (BlockStateRegistry::getPropertyIndex(placedStairs, PropIndices::SHAPE) != PropIndices::SHAPE_STRAIGHT) {
        return fail("stairs should return to shape=straight when the corner neighbor is removed");
    }

    world.setBlockState(8, baseY, 0, BlockIds::AIR);
    world.setBlockState(9, baseY, 0, BlockIds::AIR);
    world.setBlockState(9, baseY, 0, makeBottomStairs(PropIndices::FACING_EAST));
    world.setBlockState(8, baseY, 0, makeBottomStairs(PropIndices::FACING_NORTH));
    placedStairs = world.getBlockState(9, baseY, 0);
    if (BlockStateRegistry::getPropertyIndex(placedStairs, PropIndices::SHAPE) != PropIndices::SHAPE_INNER_LEFT) {
        return fail("back perpendicular stairs should update the source stair to an inner-left corner");
    }

    for (int y = baseY; y <= baseY + 4; ++y) {
        world.setBlockState(0, y, 0, BlockIds::AIR);
    }
    world.setBlockState(0, baseY, 0, BlockIds::STONE);
    world.setBlockState(0, baseY + 1, 0, floorTorch);

    const PhysicsInfo missTorchRay(
        glm::vec3(0.75f, static_cast<float>(baseY) + 2.5f, 0.75f),
        glm::vec3(0.0f, -1.0f, 0.0f));
    const RayHit missTorchHit = world.raycast(missTorchRay, 6.0f);
    if (!missTorchHit.hit || missTorchHit.blockPos != glm::ivec3(0, baseY, 0)) {
        return fail("ray missing torch selection box should continue to the support block below");
    }

    const PhysicsInfo hitTorchRay(
        glm::vec3(0.5f, static_cast<float>(baseY) + 2.5f, 0.5f),
        glm::vec3(0.0f, -1.0f, 0.0f));
    const RayHit hitTorchHit = world.raycast(hitTorchRay, 6.0f);
    if (!hitTorchHit.hit || hitTorchHit.blockPos != glm::ivec3(0, baseY + 1, 0)) {
        return fail("ray through torch selection box should target the torch");
    }

    std::cout << "[block_selection_test] PASS\n";
    return EXIT_SUCCESS;
}
