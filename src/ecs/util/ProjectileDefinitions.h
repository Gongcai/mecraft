#ifndef MECRAFT_ECS_PROJECTILE_DEFINITIONS_H
#define MECRAFT_ECS_PROJECTILE_DEFINITIONS_H

#include "../components/ProjectileComponents.h"
#include "../../item/Item.h"
#include "../../world/block/Block.h"

namespace ecs {

inline BlockID defaultProjectileEntityImpactParticleBlock() {
    if (BlockIds::ROSE != 0) {
        return BlockIds::ROSE;
    }
    if (BlockIds::DIRT != 0) {
        return BlockIds::DIRT;
    }
    return BlockIds::STONE;
}

inline ProjectileDefinition makeAppleProjectileDefinition() {
    ProjectileDefinition definition;
    definition.itemId = ItemIds::APPLE;
    definition.damage = 5;
    definition.hitRadius = 0.45f;
    definition.gravity = 8.0f;
    definition.throwSpeed = 15.0f;
    definition.upwardBias = 1.2f;
    definition.spawnForwardOffset = 0.75f;
    definition.boundsHalfExtent = 0.22f;
    definition.lifetimeSeconds = 4.0f;
    definition.spinSpeedRadians = 10.0f;
    definition.entityImpactParticleBlock = defaultProjectileEntityImpactParticleBlock();
    return definition;
}

inline bool getThrowableProjectileDefinition(const ItemID itemId, ProjectileDefinition& outDefinition) {
    if (itemId != 0 && itemId == ItemIds::APPLE) {
        outDefinition = makeAppleProjectileDefinition();
        return true;
    }
    return false;
}

inline ProjectileDefinition projectileDefinitionForItemOrDefault(const ItemID itemId) {
    ProjectileDefinition definition;
    if (getThrowableProjectileDefinition(itemId, definition)) {
        return definition;
    }

    definition.itemId = itemId;
    definition.entityImpactParticleBlock = defaultProjectileEntityImpactParticleBlock();
    return definition;
}

} // namespace ecs

#endif // MECRAFT_ECS_PROJECTILE_DEFINITIONS_H
