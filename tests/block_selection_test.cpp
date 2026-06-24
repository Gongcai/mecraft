#include <cstdlib>
#include <iostream>
#include <utility>
#include <vector>

#include <glm/vec3.hpp>

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

    World world;
    world.init(20260507);
    loadChunks(world);

    const int baseY = world.getSurfaceY(0, 0) + 2;
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
