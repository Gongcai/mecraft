#include <cstdlib>
#include <iostream>
#include <utility>
#include <vector>

#include <glm/vec3.hpp>

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
    if (eastBox.min.x <= 0.45f || eastBox.max.y < 0.7f) {
        return fail("east wall torch selection box should rotate onto the X axis");
    }

    const StateID westTorch = BlockStateRegistry::getState(
        BlockIds::TORCH,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, PropIndices::FACING_WEST}
        });
    const BlockSelectionBox westBox = BlockSelection::getBox(westTorch);
    if (westBox.max.x >= 0.55f || westBox.max.y < 0.7f) {
        return fail("west wall torch selection box should mirror the east wall model");
    }

    const BlockSelectionBox crossBox = BlockSelection::getBox(BlockIds::TALL_GRASS);
    if (crossBox.min.x <= 0.1f || crossBox.max.x >= 0.9f ||
        crossBox.min.z <= 0.1f || crossBox.max.z >= 0.9f) {
        return fail("cross-shaped plants should not use a full horizontal voxel selection box");
    }

    World world;
    world.init(20260507);
    loadChunks(world);

    const int baseY = world.getSurfaceY(0, 0) + 2;
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
