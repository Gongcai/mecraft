#ifndef MECRAFT_ECS_UTIL_SIMULATION_DISTANCE_H
#define MECRAFT_ECS_UTIL_SIMULATION_DISTANCE_H

#include "../SystemContext.h"
#include "../components/Components.h"
#include "../../world/World.h"
#include "../../world/chunk/Chunk.h"

#include <cmath>

namespace ecs::simulation {

inline int toChunkCoord(const float worldCoord, const int chunkSize) {
    return static_cast<int>(std::floor(worldCoord / static_cast<float>(chunkSize)));
}

inline bool isPositionTicking(const SystemContext& ctx, const glm::vec3& position) {
    const World* world = ctx.services.world.get();
    if (world == nullptr) {
        return true;
    }

    const int chunkX = toChunkCoord(position.x, Chunk::SIZE_X);
    const int chunkZ = toChunkCoord(position.z, Chunk::SIZE_Z);
    return world->ticketManager().shouldTick(chunkX, chunkZ);
}

inline bool isBlockPositionTicking(const SystemContext& ctx, const glm::ivec3& position) {
    const World* world = ctx.services.world.get();
    if (world == nullptr) {
        return true;
    }

    const int chunkX = static_cast<int>(std::floor(static_cast<float>(position.x) /
                                                   static_cast<float>(Chunk::SIZE_X)));
    const int chunkZ = static_cast<int>(std::floor(static_cast<float>(position.z) /
                                                   static_cast<float>(Chunk::SIZE_Z)));
    return world->ticketManager().shouldTick(chunkX, chunkZ);
}

inline bool isEntityTicking(const SystemContext& ctx, const entt::entity entity) {
    auto& reg = ctx.registry.registry();
    if (reg.all_of<LocalPlayerTag>(entity)) {
        return true;
    }
    if (const auto* transform = reg.try_get<TransformComponent>(entity)) {
        return isPositionTicking(ctx, transform->position);
    }
    if (const auto* physicsBody = reg.try_get<PhysicsBodyComponent>(entity)) {
        return isPositionTicking(ctx, physicsBody->body.position);
    }
    return true;
}

} // namespace ecs::simulation

#endif // MECRAFT_ECS_UTIL_SIMULATION_DISTANCE_H
