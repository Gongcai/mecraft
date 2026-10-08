#ifndef MECRAFT_ECS_PARTICLE_PHYSICS_HELPERS_H
#define MECRAFT_ECS_PARTICLE_PHYSICS_HELPERS_H

#include <algorithm>
#include <array>
#include <cmath>

#include <glm/glm.hpp>

#include "DropPhysicsHelpers.h"
#include "../components/Components.h"
#include "../../world/World.h"

namespace ecs::particle_detail {

// Particles collide as small boxes so that they visibly rest on block tops
// instead of passing through the voxel grid.
constexpr float kParticleHalfExtent = 0.03f;
constexpr float kParticleGravity = 14.0f;
// Sub-step length; keeps fast particles from tunneling through thin collision boxes.
constexpr float kParticleAxisStep = 0.1f;
// Exponential decay rate (1/s) applied to horizontal velocity while resting on a block.
constexpr float kParticleGroundFriction = 6.0f;

/// Moves a particle along one axis in sub-steps, stopping at solid blocks.
/// A particle that already overlaps a block at the start of the step is allowed
/// to move freely, so particles spawned inside a broken or hit block drift out
/// instead of freezing in place.
/// @param axis 0 = X, 1 = Y, 2 = Z.
inline void moveParticleAxis(TransformComponent& transform, VelocityComponent& velocity, ParticleComponent& particle,
                             const World& world, const int axis, const float dt) {
    const glm::vec3 halfExtents(kParticleHalfExtent);
    const float delta = velocity.velocity[axis] * dt;
    if (std::abs(delta) <= 0.0f) {
        return;
    }

    const int steps = std::max(1, static_cast<int>(std::ceil(std::abs(delta) / kParticleAxisStep)));
    const float stepDelta = delta / static_cast<float>(steps);

    for (int i = 0; i < steps; ++i) {
        const glm::vec3 previousPos = transform.position;
        transform.position[axis] += stepDelta;

        if (!drop_detail::overlapsCollision(world, transform.position, halfExtents)) {
            continue;
        }
        if (drop_detail::overlapsCollision(world, previousPos, halfExtents)) {
            // Already inside a block before this step: let it escape.
            continue;
        }

        transform.position = previousPos;
        velocity.velocity[axis] = 0.0f;
        if (axis == 1 && stepDelta < 0.0f) {
            particle.grounded = true;
        }
        return;
    }
}

/// Moves a particle that starts inside a solid block to the nearest free point.
/// Directions are tried in the order +Y, -Y, +X, -X, +Z, -Z, so an embedded
/// particle is ejected through the top of the block whenever that is free.
/// @param position Current particle center; updated in place when a free point is found.
inline void ejectFromSolid(glm::vec3& position, const World& world) {
    const glm::vec3 halfExtents(kParticleHalfExtent);
    if (!drop_detail::overlapsCollision(world, position, halfExtents)) {
        return;
    }

    constexpr std::array<glm::vec3, 6> kEjectDirections = {
        glm::vec3(0.0f, 1.0f, 0.0f),  glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f),  glm::vec3(0.0f, 0.0f, -1.0f),
    };
    constexpr float kEjectSearchStep = 0.05f;
    constexpr float kEjectSearchDistance = 1.0f;

    for (const glm::vec3& direction : kEjectDirections) {
        for (float distance = kEjectSearchStep; distance <= kEjectSearchDistance; distance += kEjectSearchStep) {
            const glm::vec3 candidate = position + direction * distance;
            if (!drop_detail::overlapsCollision(world, candidate, halfExtents)) {
                position = candidate;
                return;
            }
        }
    }
    // No free point within reach: leave the particle in place; the escape rule in moveParticleAxis lets it drift.
}

/// Advances one particle by dt seconds: ejection from embedded blocks, gravity,
/// collision-resolved motion per axis, then ground friction when resting on a block.
/// @param world Voxel world used for collision queries.
inline void simulateParticleStep(TransformComponent& transform, VelocityComponent& velocity,
                                 ParticleComponent& particle, const World& world, const float dt) {
    ejectFromSolid(transform.position, world);

    particle.grounded = false;
    velocity.velocity.y -= kParticleGravity * dt;

    moveParticleAxis(transform, velocity, particle, world, 1, dt);
    moveParticleAxis(transform, velocity, particle, world, 0, dt);
    moveParticleAxis(transform, velocity, particle, world, 2, dt);

    if (particle.grounded) {
        const float friction = std::exp(-kParticleGroundFriction * dt);
        velocity.velocity.x *= friction;
        velocity.velocity.z *= friction;
    }
}

} // namespace ecs::particle_detail

#endif // MECRAFT_ECS_PARTICLE_PHYSICS_HELPERS_H
