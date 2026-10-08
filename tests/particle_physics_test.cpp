#include <cmath>
#include <cstdlib>
#include <iostream>

#include <glm/vec3.hpp>

#include "../src/ecs/components/ParticleComponents.h"
#include "../src/ecs/components/TransformComponents.h"
#include "../src/ecs/components/PhysicsComponents.h"
#include "../src/ecs/util/DropPhysicsHelpers.h"
#include "../src/ecs/util/ParticlePhysicsHelpers.h"
#include "../src/world/World.h"
#include "../src/world/block/Block.h"

namespace {

constexpr float kDt = 1.0f / 60.0f;
constexpr float kPositionEpsilon = 0.01f;

int fail(const char* message) {
    std::cerr << "[particle_physics_test] FAIL: " << message << '\n';
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
    BlockRegistry::init(nullptr);

    World world;
    world.init(20260328);
    loadChunks(world);

    const int surfaceY = world.getSurfaceY(0, 0);
    const float expectedRestY = static_cast<float>(surfaceY) + 1.0f + ecs::particle_detail::kParticleHalfExtent;

    // Case 1: a falling particle must stop on the block top instead of passing through it.
    ecs::TransformComponent falling;
    falling.position = glm::vec3(0.5f, static_cast<float>(surfaceY) + 6.0f, 0.5f);
    ecs::VelocityComponent fallingVelocity;
    ecs::ParticleComponent fallingParticle;

    for (int i = 0; i < 240; ++i) {
        ecs::particle_detail::simulateParticleStep(falling, fallingVelocity, fallingParticle, world, kDt);
    }

    if (!fallingParticle.grounded) {
        return fail("falling particle should be grounded on the surface block");
    }
    if (std::abs(falling.position.y - expectedRestY) > kPositionEpsilon) {
        return fail("falling particle should rest on the block top");
    }
    if (std::abs(fallingVelocity.velocity.y) > kPositionEpsilon) {
        return fail("vertical velocity should be cleared while resting");
    }

    // Case 2: a particle spawned inside a solid block must be ejected and must not sink through the ground.
    ecs::TransformComponent embedded;
    embedded.position = glm::vec3(0.5f, static_cast<float>(surfaceY) + 0.5f, 0.5f);
    ecs::VelocityComponent embeddedVelocity;
    embeddedVelocity.velocity = glm::vec3(0.0f, 3.0f, 0.0f);
    ecs::ParticleComponent embeddedParticle;

    for (int i = 0; i < 30; ++i) {
        ecs::particle_detail::simulateParticleStep(embedded, embeddedVelocity, embeddedParticle, world, kDt);
        if (ecs::drop_detail::overlapsCollision(world, embedded.position,
                                           glm::vec3(ecs::particle_detail::kParticleHalfExtent))) {
            return fail("particle should never remain inside a solid block");
        }
    }

    if (embedded.position.y < static_cast<float>(surfaceY) + 1.0f) {
        return fail("particle spawned inside a block should not sink through the ground");
    }

    std::cout << "[particle_physics_test] PASS\n";
    return EXIT_SUCCESS;
}
