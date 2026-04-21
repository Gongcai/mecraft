#ifndef MECRAFT_ECS_DROP_PHYSICS_HELPERS_H
#define MECRAFT_ECS_DROP_PHYSICS_HELPERS_H

#include <algorithm>
#include <cmath>

#include "../components/Components.h"
#include "../../world/World.h"

namespace ecs::drop_detail {

constexpr float kAxisStep = 0.2f;
constexpr float kContactEpsilon = 0.0005f;

inline bool isSolidBlock(const World& world, const int x, const int y, const int z) {
    const BlockID id = world.getBlock(x, y, z);
    if (id == 0) {
        return false;
    }
    return BlockRegistry::get(id).isSolid;
}

inline bool overlapsSolid(const World& world, const glm::vec3& center, const glm::vec3& halfExtents) {
    const glm::vec3 minPos = center - halfExtents;
    const glm::vec3 maxPos = center + halfExtents;

    const int minX = static_cast<int>(std::floor(minPos.x));
    const int maxX = static_cast<int>(std::floor(maxPos.x - kContactEpsilon));
    const int minY = static_cast<int>(std::floor(minPos.y));
    const int maxY = static_cast<int>(std::floor(maxPos.y - kContactEpsilon));
    const int minZ = static_cast<int>(std::floor(minPos.z));
    const int maxZ = static_cast<int>(std::floor(maxPos.z - kContactEpsilon));

    for (int x = minX; x <= maxX; ++x) {
        for (int y = minY; y <= maxY; ++y) {
            for (int z = minZ; z <= maxZ; ++z) {
                if (isSolidBlock(world, x, y, z)) {
                    return true;
                }
            }
        }
    }

    return false;
}

inline bool overlapsBlockAabb(const glm::vec3& position,
                              const glm::vec3& halfExtents,
                              const glm::ivec3& blockPos) {
    const glm::vec3 minPos = position - halfExtents;
    const glm::vec3 maxPos = position + halfExtents;

    const auto blockMinX = static_cast<float>(blockPos.x);
    const auto blockMinY = static_cast<float>(blockPos.y);
    const auto blockMinZ = static_cast<float>(blockPos.z);
    const float blockMaxX = blockMinX + 1.0f;
    const float blockMaxY = blockMinY + 1.0f;
    const float blockMaxZ = blockMinZ + 1.0f;

    return minPos.x < blockMaxX && maxPos.x > blockMinX &&
           minPos.y < blockMaxY && maxPos.y > blockMinY &&
           minPos.z < blockMaxZ && maxPos.z > blockMinZ;
}

inline void moveAndCollideAxis(TransformComponent& transform,
                               VelocityComponent& velocity,
                               const BoundsComponent& bounds,
                               GroundedStateComponent& grounded,
                               const World& world,
                               const int axis,
                               const float dt) {
    const float delta = velocity.velocity[axis] * dt;
    if (std::abs(delta) <= 0.0f) {
        return;
    }

    const int steps = std::max(1, static_cast<int>(std::ceil(std::abs(delta) / kAxisStep)));
    const float stepDelta = delta / static_cast<float>(steps);

    for (int i = 0; i < steps; ++i) {
        const glm::vec3 previousPos = transform.position;
        transform.position[axis] += stepDelta;

        if (!overlapsSolid(world, transform.position, bounds.halfExtents)) {
            continue;
        }

        transform.position = previousPos;
        velocity.velocity[axis] = 0.0f;

        if (axis == 1 && stepDelta < 0.0f) {
            grounded.grounded = true;
        }
        return;
    }
}

} // namespace ecs::drop_detail

#endif // MECRAFT_ECS_DROP_PHYSICS_HELPERS_H
