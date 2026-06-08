#ifndef MECRAFT_ECS_PROJECTILE_COMPONENTS_H
#define MECRAFT_ECS_PROJECTILE_COMPONENTS_H

#include <entt/entity/entity.hpp>

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
};

struct ProjectileComponent {
    entt::entity owner = entt::null;
    int damage = 4;
    float hitRadius = 0.45f;
    float gravity = 8.0f;
    BlockID entityImpactParticleBlock = 0;
};

struct ProjectileThrowerComponent {
    float cooldownRemaining = 0.0f;
    float cooldownSeconds = 0.55f;
};

} // namespace ecs

#endif // MECRAFT_ECS_PROJECTILE_COMPONENTS_H
