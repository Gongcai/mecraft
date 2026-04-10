#include <cmath>
#include <cstdlib>
#include <iostream>

#include <glm/vec3.hpp>

#include "../src/physics/PhysicsSystem.h"
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

} // namespace

int main() {
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
    world.setBlock(2, surfaceY + 1, 0, BlockType::STONE);

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
                world.setBlock(x, y, z, BlockType::AIR);
            }
        }
    }
    for (int x = 0; x <= 5; ++x) {
        world.setBlock(x, stripY, 14, BlockType::STONE);
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
                world.setBlock(x, y, z, BlockType::AIR);
            }
        }
    }
    for (int x = 0; x <= 35; ++x) {
        world.setBlock(x, speedLaneY, 17, BlockType::STONE);
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
                world.setBlock(x, y, z, BlockType::AIR);
            }
        }
    }
    world.setBlock(0, platformY, 8, BlockType::STONE);
    world.setBlock(1, platformY, 8, BlockType::STONE);
    world.setBlock(2, platformY, 8, BlockType::STONE);

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

    std::cout << "[physics_mvp_test] PASS\n";
    return EXIT_SUCCESS;
}
