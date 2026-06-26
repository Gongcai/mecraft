#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

#include <glm/vec3.hpp>

#include "../src/physics/PhysicsSystem.h"
#include "../src/world/block/BlockCollision.h"
#include "../src/world/fluid/FluidState.h"
#include "../src/world/block/BlockStateRegistry.h"
#include "../src/world/block/PropIndices.h"
#include "../src/world/World.h"

namespace {

constexpr float kDt = 1.0f / 60.0f;

int fail(const char* message) {
    std::cerr << "[physics_mvp_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

void loadChunks(World& world) {
    world.setRenderDistance(1);
    for (int i = 0; i < 8; ++i) {
        world.update(glm::vec3(0.0f, 0.0f, 0.0f));
    }
}

bool bodyOverlapsWorld(const World& world, const PhysicsBody& body) {
    const glm::vec3 center = body.position + body.colliderOffset;
    const glm::vec3 queryMin = center - body.halfExtents;
    const glm::vec3 queryMax = center + body.halfExtents;
    const int minX = static_cast<int>(std::floor(queryMin.x));
    const int maxX = static_cast<int>(std::floor(queryMax.x - 0.001f));
    const int minY = static_cast<int>(std::floor(queryMin.y));
    const int maxY = static_cast<int>(std::floor(queryMax.y - 0.001f));
    const int minZ = static_cast<int>(std::floor(queryMin.z));
    const int maxZ = static_cast<int>(std::floor(queryMax.z - 0.001f));

    for (int x = minX; x <= maxX; ++x) {
        for (int y = minY; y <= maxY; ++y) {
            for (int z = minZ; z <= maxZ; ++z) {
                const StateID state = world.getBlockState(x, y, z);
                if (BlockCollision::intersects(state, glm::ivec3(x, y, z), queryMin, queryMax)) {
                    return true;
                }
            }
        }
    }
    return false;
}

} // namespace

int main() {
    BlockRegistry::init(nullptr);

    World world;
    world.init(20260328);
    loadChunks(world);

    const int surfaceY = world.getSurfaceY(0, 0);

    PhysicsTuning tuning;

    // Case 1: free-fall should land on the flat ground.
    PhysicsBody falling;
    falling.position = glm::vec3(0.5f, static_cast<float>(surfaceY) + 6.0f, 0.5f);

    physics::PhysicsSystem phys(&world);

    MoveIntent idleIntent{};
    for (int i = 0; i < 600; ++i) {
        phys.updateBody(falling, idleIntent, kDt);
    }

    const float expectedGroundY = static_cast<float>(surfaceY) + 1.0f + falling.halfExtents.y;
    if (!falling.isGrounded) {
        return fail("falling body did not become grounded");
    }
    if (std::abs(falling.position.y - expectedGroundY) > 0.03f) {
        return fail("falling body settled at unexpected Y");
    }
    if (std::abs(falling.velocity.y) > 0.01f) {
        return fail("vertical velocity should be near zero after landing");
    }

    // Case 2: moving into a wall should stop horizontal movement.
    world.setBlock(2, surfaceY + 1, 0, BlockRegistry::requireIdByName("minecraft:stone"));

    PhysicsBody runner;
    runner.position = glm::vec3(0.5f, expectedGroundY, 0.5f);
    runner.isGrounded = true;

    MoveIntent runRight{};
    runRight.move = glm::vec2(1.0f, 0.0f);

    for (int i = 0; i < 180; ++i) {
        phys.updateBody(runner, runRight, kDt);
    }

    if (!runner.hitWall) {
        return fail("runner should report wall contact");
    }
    if (runner.position.x > 1.71f) {
        return fail("runner passed through wall on X axis");
    }

    // Case 3: crouching on fully supported ground should still allow movement.
    const int stripY = surfaceY + 4;
    for (int x = -2; x <= 8; ++x) {
        for (int y = surfaceY + 1; y <= stripY + 2; ++y) {
            for (int z = 13; z <= 15; ++z) {
                world.setBlock(x, y, z, RUNTIME_ID_NULL);
            }
        }
    }
    for (int x = 0; x <= 5; ++x) {
        world.setBlock(x, stripY, 14, BlockRegistry::requireIdByName("minecraft:stone"));
    }

    const float stripTopY = static_cast<float>(stripY) + 1.0f + runner.halfExtents.y;

    PhysicsBody crouchRunner;
    crouchRunner.position = glm::vec3(0.5f, stripTopY + 0.0008f, 14.5f);
    crouchRunner.isGrounded = true;

    MoveIntent crouchRun{};
    crouchRun.move = glm::vec2(1.0f, 0.0f);
    crouchRun.wantsCrouch = true;

    for (int i = 0; i < 120; ++i) {
        phys.updateBody(crouchRunner, crouchRun, kDt);
    }

    if (crouchRunner.position.x <= 2.0f) {
        return fail("crouching body should move when there is ground support");
    }

    // Case 3.5: crouch speed should be 50% of sprint speed on flat ground.
    const int speedLaneY = surfaceY + 5;
    for (int x = -2; x <= 40; ++x) {
        for (int y = surfaceY + 1; y <= speedLaneY + 2; ++y) {
            for (int z = 16; z <= 18; ++z) {
                world.setBlock(x, y, z, RUNTIME_ID_NULL);
            }
        }
    }
    for (int x = 0; x <= 35; ++x) {
        world.setBlock(x, speedLaneY, 17, BlockRegistry::requireIdByName("minecraft:stone"));
    }

    const float speedLaneTopY = static_cast<float>(speedLaneY) + 1.0f + runner.halfExtents.y;

    PhysicsBody sprintRunner;
    sprintRunner.position = glm::vec3(0.5f, speedLaneTopY, 17.5f);
    sprintRunner.isGrounded = true;

    PhysicsBody crouchSpeedRunner;
    crouchSpeedRunner.position = glm::vec3(0.5f, speedLaneTopY, 17.5f);
    crouchSpeedRunner.isGrounded = true;

    MoveIntent sprintMove{};
    sprintMove.move = glm::vec2(1.0f, 0.0f);
    sprintMove.wantsSprint = true;

    MoveIntent crouchSpeedMove{};
    crouchSpeedMove.move = glm::vec2(1.0f, 0.0f);
    crouchSpeedMove.wantsCrouch = true;

    for (int i = 0; i < 120; ++i) {
        phys.updateBody(sprintRunner, sprintMove, kDt);
        phys.updateBody(crouchSpeedRunner, crouchSpeedMove, kDt);
    }

    const float sprintDistance = sprintRunner.position.x - 0.5f;
    const float crouchDistance = crouchSpeedRunner.position.x - 0.5f;
    if (sprintDistance <= 0.05f) {
        return fail("sprint runner should have moved forward");
    }
    const float crouchToSprintRatio = crouchDistance / sprintDistance;
    if (std::abs(crouchToSprintRatio - 0.5f) > 0.08f) {
        return fail("crouch speed should be about half of sprint speed");
    }

    // Case 4: crouching at a ledge should prevent moving into unsupported space.
    const int platformY = surfaceY + 2;
    for (int x = -5; x <= 20; ++x) {
        for (int y = surfaceY + 1; y <= platformY + 2; ++y) {
            for (int z = 5; z <= 12; ++z) {
                world.setBlock(x, y, z, RUNTIME_ID_NULL);
            }
        }
    }
    world.setBlock(0, platformY, 8, BlockRegistry::requireIdByName("minecraft:stone"));
    world.setBlock(1, platformY, 8, BlockRegistry::requireIdByName("minecraft:stone"));
    world.setBlock(2, platformY, 8, BlockRegistry::requireIdByName("minecraft:stone"));

    const float platformTopY = static_cast<float>(platformY) + 1.0f + runner.halfExtents.y;

    PhysicsBody croucher;
    croucher.position = glm::vec3(1.5f, platformTopY, 8.5f);
    croucher.isGrounded = true;

    MoveIntent crouchMove{};
    crouchMove.move = glm::vec2(1.0f, 0.0f);
    crouchMove.wantsCrouch = true;

    for (int i = 0; i < 240; ++i) {
        phys.updateBody(croucher, crouchMove, kDt);
    }

    if (!croucher.isGrounded) {
        return fail("crouching body should stay grounded near ledge");
    }
    if (croucher.position.y < platformTopY - 0.03f) {
        return fail("crouching body should not fall from platform edge");
    }

    // Case 5: same movement without crouch should be able to drop off the ledge.
    PhysicsBody walker;
    walker.position = glm::vec3(1.5f, platformTopY, 8.5f);
    walker.isGrounded = true;

    MoveIntent walkOff{};
    walkOff.move = glm::vec2(1.0f, 0.0f);

    bool fellOffPlatform = false;
    for (int i = 0; i < 240; ++i) {
        phys.updateBody(walker, walkOff, kDt);
        if (walker.position.y < platformTopY - 0.3f) {
            fellOffPlatform = true;
            break;
        }
    }

    if (!fellOffPlatform) {
        return fail("non-crouching body should be able to walk off ledge");
    }

    // Case 6: model blocks should collide at their model element height.
    const BlockID oakSlab = BlockRegistry::findByName("oak_slab");
    if (oakSlab == RUNTIME_ID_NULL) {
        return fail("oak_slab should be registered for model collision physics");
    }
    const StateID bottomSlab = BlockStateRegistry::getDefaultState(oakSlab);
    const StateID topSlab = BlockStateRegistry::getState(
        oakSlab,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::HALF, PropIndices::HALF_TOP}
        });

    const int slabY = surfaceY + 9;
    for (int x = -2; x <= 2; ++x) {
        for (int y = surfaceY + 1; y <= slabY + 4; ++y) {
            for (int z = 20; z <= 24; ++z) {
                world.setBlock(x, y, z, RUNTIME_ID_NULL);
            }
        }
    }

    world.setBlock(0, slabY, 21, bottomSlab);
    PhysicsBody bottomSlabBody;
    bottomSlabBody.position = glm::vec3(0.5f, static_cast<float>(slabY) + 4.0f, 21.5f);
    for (int i = 0; i < 600; ++i) {
        phys.updateBody(bottomSlabBody, idleIntent, kDt);
    }

    const float bottomSlabBodyY = static_cast<float>(slabY) + 0.5f + bottomSlabBody.halfExtents.y;
    if (!bottomSlabBody.isGrounded) {
        return fail("body should land on a bottom slab collision box");
    }
    if (std::abs(bottomSlabBody.position.y - bottomSlabBodyY) > 0.03f) {
        return fail("bottom slab collision should settle at half-block height");
    }

    world.setBlock(0, slabY, 21, RUNTIME_ID_NULL);
    world.setBlock(0, slabY, 23, topSlab);
    PhysicsBody topSlabBody;
    topSlabBody.position = glm::vec3(0.5f, static_cast<float>(slabY) + 4.0f, 23.5f);
    for (int i = 0; i < 600; ++i) {
        phys.updateBody(topSlabBody, idleIntent, kDt);
    }

    const float topSlabBodyY = static_cast<float>(slabY) + 1.0f + topSlabBody.halfExtents.y;
    if (!topSlabBody.isGrounded) {
        return fail("body should land on a top slab collision box");
    }
    if (std::abs(topSlabBody.position.y - topSlabBodyY) > 0.03f) {
        return fail("top slab collision should settle at full-block height");
    }

    // Case 7: grounded movement should step onto half-height model blocks.
    const int stepLaneY = surfaceY + 13;
    for (int x = -2; x <= 14; ++x) {
        for (int y = surfaceY + 1; y <= stepLaneY + 4; ++y) {
            for (int z = 25; z <= 29; ++z) {
                world.setBlock(x, y, z, RUNTIME_ID_NULL);
            }
        }
    }
    for (int x = -1; x <= 13; ++x) {
        world.setBlock(x, stepLaneY, 26, BlockRegistry::requireIdByName("minecraft:stone"));
    }
    for (int x = 1; x <= 12; ++x) {
        world.setBlock(x, stepLaneY + 1, 26, bottomSlab);
    }

    PhysicsBody slabStepper;
    slabStepper.position = glm::vec3(0.5f,
                                     static_cast<float>(stepLaneY) + 1.0f + slabStepper.halfExtents.y,
                                     26.5f);
    slabStepper.isGrounded = true;

    MoveIntent stepForward{};
    stepForward.move = glm::vec2(1.0f, 0.0f);
    for (int i = 0; i < 120; ++i) {
        phys.updateBody(slabStepper, stepForward, kDt);
    }

    const float slabStepY = static_cast<float>(stepLaneY) + 1.5f + slabStepper.halfExtents.y;
    if (slabStepper.position.x <= 1.25f) {
        return fail("body should move onto a bottom slab instead of treating it as a wall");
    }
    if (std::abs(slabStepper.position.y - slabStepY) > 0.08f) {
        return fail("body should step up to bottom slab height during horizontal movement");
    }

    const BlockID oakStairs = BlockRegistry::findByName("oak_stairs");
    if (oakStairs == RUNTIME_ID_NULL) {
        return fail("oak_stairs should be registered for stair step physics");
    }
    const StateID eastStairs = BlockStateRegistry::getDefaultState(oakStairs);
    for (int x = -2; x <= 14; ++x) {
        for (int y = surfaceY + 1; y <= stepLaneY + 4; ++y) {
            for (int z = 30; z <= 34; ++z) {
                world.setBlock(x, y, z, RUNTIME_ID_NULL);
            }
        }
    }
    for (int x = -1; x <= 13; ++x) {
        world.setBlock(x, stepLaneY, 31, BlockRegistry::requireIdByName("minecraft:stone"));
    }
    for (int x = 1; x <= 12; ++x) {
        world.setBlock(x, stepLaneY + 1, 31, eastStairs);
    }

    PhysicsBody stairStepper;
    stairStepper.position = glm::vec3(0.5f,
                                      static_cast<float>(stepLaneY) + 1.0f + stairStepper.halfExtents.y,
                                      31.5f);
    stairStepper.isGrounded = true;
    for (int i = 0; i < 120; ++i) {
        phys.updateBody(stairStepper, stepForward, kDt);
    }

    if (stairStepper.position.x <= 1.25f) {
        return fail("body should move onto stairs instead of treating them as a wall");
    }
    if (stairStepper.position.y <= static_cast<float>(stepLaneY) + 1.0f + stairStepper.halfExtents.y + 0.2f) {
        return fail("body should gain height while stepping through stairs");
    }

    // Case 8: pre-existing block overlap should allow movement that exits the collision volume.
    const int doorFloorY = surfaceY + 17;
    const int doorLowerY = doorFloorY + 1;
    const int doorZ = 37;
    for (int x = -2; x <= 2; ++x) {
        for (int y = surfaceY + 1; y <= doorLowerY + 3; ++y) {
            for (int z = doorZ - 2; z <= doorZ + 3; ++z) {
                world.setBlock(x, y, z, RUNTIME_ID_NULL);
            }
        }
    }
    for (int x = -2; x <= 2; ++x) {
        for (int z = doorZ - 2; z <= doorZ + 3; ++z) {
            world.setBlock(x, doorFloorY, z, BlockRegistry::requireIdByName("minecraft:stone"));
        }
    }

    const StateID openDoorLower = BlockStateRegistry::getState(
        BlockRegistry::requireIdByName("minecraft:oak_door"),
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, PropIndices::FACING_EAST},
            {PropIndices::HALF, PropIndices::HALF_LOWER},
            {PropIndices::HINGE, PropIndices::HINGE_LEFT},
            {PropIndices::OPEN, PropIndices::OPEN_TRUE},
            {PropIndices::POWERED, PropIndices::POWERED_FALSE}
        });
    const StateID openDoorUpper = BlockStateRegistry::getState(
        BlockRegistry::requireIdByName("minecraft:oak_door"),
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, PropIndices::FACING_EAST},
            {PropIndices::HALF, PropIndices::HALF_UPPER},
            {PropIndices::HINGE, PropIndices::HINGE_LEFT},
            {PropIndices::OPEN, PropIndices::OPEN_TRUE},
            {PropIndices::POWERED, PropIndices::POWERED_FALSE}
        });
    world.setBlock(0, doorLowerY, doorZ, openDoorLower);
    world.setBlock(0, doorLowerY + 1, doorZ, openDoorUpper);

    PhysicsBody doorEscaper;
    doorEscaper.position = glm::vec3(0.5f, static_cast<float>(doorLowerY) + doorEscaper.halfExtents.y, doorZ + 0.05f);
    doorEscaper.isGrounded = true;
    if (!bodyOverlapsWorld(world, doorEscaper)) {
        return fail("door escape test body should start inside the opened door collision");
    }

    MoveIntent moveOutOfDoor{};
    moveOutOfDoor.move = glm::vec2(0.0f, 1.0f);
    for (int i = 0; i < 80; ++i) {
        phys.updateBody(doorEscaper, moveOutOfDoor, kDt);
    }
    if (doorEscaper.position.z <= static_cast<float>(doorZ) + 0.49f ||
        bodyOverlapsWorld(world, doorEscaper)) {
        return fail("body starting inside an opened door should be able to move out of the door collision");
    }

    PhysicsBody doorBlockedWalker;
    doorBlockedWalker.position =
        glm::vec3(0.5f, static_cast<float>(doorLowerY) + doorBlockedWalker.halfExtents.y, doorZ + 0.55f);
    doorBlockedWalker.isGrounded = true;
    if (bodyOverlapsWorld(world, doorBlockedWalker)) {
        return fail("door blocked-walker test body should start outside the opened door collision");
    }

    MoveIntent moveIntoDoor{};
    moveIntoDoor.move = glm::vec2(0.0f, -1.0f);
    for (int i = 0; i < 80; ++i) {
        phys.updateBody(doorBlockedWalker, moveIntoDoor, kDt);
    }
    if (doorBlockedWalker.position.z < static_cast<float>(doorZ) + 0.48f ||
        bodyOverlapsWorld(world, doorBlockedWalker)) {
        return fail("body starting outside an opened door should still be blocked from entering its collision");
    }

    // Case 9: fluid flow should impart a repeatable horizontal push.
    const int flowY = surfaceY + 8;
    for (int x = -2; x <= 4; ++x) {
        for (int y = surfaceY + 1; y <= flowY + 2; ++y) {
            for (int z = -1; z <= 1; ++z) {
                world.setBlock(x, y, z, RUNTIME_ID_NULL);
            }
        }
    }
    for (int x = -2; x <= 4; ++x) {
        for (int z = -1; z <= 1; ++z) {
            world.setBlock(x, flowY - 1, z, BlockRegistry::requireIdByName("minecraft:stone"));
        }
    }
    world.setBlock(0, flowY, 0, FluidState::makeWater(0, false));
    world.setBlock(1, flowY, 0, FluidState::makeWater(3, false));

    PhysicsBody floater;
    floater.position = glm::vec3(0.5f, static_cast<float>(flowY) + 1.2f, 0.5f);

    const float startX = floater.position.x;
    bool touchedWater = false;
    for (int i = 0; i < 120; ++i) {
        phys.updateBody(floater, idleIntent, kDt);
        touchedWater = touchedWater || floater.isInWater;
    }

    if (!touchedWater) {
        return fail("flow test body should remain in water long enough to sample fluid push");
    }
    if (floater.position.x <= startX + 0.03f) {
        return fail("water flow should push bodies in the direction of the fluid vector");
    }

    std::cout << "[physics_mvp_test] PASS\n";
    return EXIT_SUCCESS;
}
