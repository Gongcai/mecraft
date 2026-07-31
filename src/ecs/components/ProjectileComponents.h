#ifndef MECRAFT_ECS_PROJECTILE_COMPONENTS_H
#define MECRAFT_ECS_PROJECTILE_COMPONENTS_H

#include <string>

#include <entt/entity/entity.hpp>
#include <glm/glm.hpp>

#include "../../item/Item.h"
#include "../../world/block/Block.h"

namespace ecs {

struct ProjectileDefinition {
    ItemID itemId = 0;
    int damage = 4;
    float hitRadius = 0.45f;
    float gravity = 8.0f;
    float throwSpeed = 15.0f;
    float upwardBias = 1.2f;
    float spawnForwardOffset = 0.75f;
    float boundsHalfExtent = 0.22f;
    float lifetimeSeconds = 4.0f;
    float spinSpeedRadians = 10.0f;
    BlockID entityImpactParticleBlock = 0;
    int entityImpactParticleCount = 14;
    std::string throwSoundId;
    std::string impactSoundId;
};

struct ProjectileComponent {
    entt::entity owner = entt::null;
    int damage = 4;
    float hitRadius = 0.45f;
    float gravity = 8.0f;
    BlockID entityImpactParticleBlock = 0;
    int entityImpactParticleCount = 14;
    std::string impactSoundId;
};

struct ProjectileThrowerComponent {
    float cooldownRemaining = 0.0f;
    float cooldownSeconds = 0.55f;
};

struct EntityImpactComponent {
    EntityImpactComponent() = default;
    EntityImpactComponent(const glm::vec3& impactPosition, const BlockID impactParticleBlock,
                          const int impactParticleCount = 14)
        : position(impactPosition), particleBlock(impactParticleBlock), particleCount(impactParticleCount) {}

    glm::vec3 position{0.0f};
    BlockID particleBlock = 0;
    int particleCount = 14;
};

} // namespace ecs

#endif // MECRAFT_ECS_PROJECTILE_COMPONENTS_H
