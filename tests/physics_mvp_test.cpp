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

    const int surfaceY = world.getFlatSurfaceY();

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

    std::cout << "[physics_mvp_test] PASS\n";
    return EXIT_SUCCESS;
}
