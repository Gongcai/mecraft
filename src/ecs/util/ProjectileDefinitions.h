#ifndef MECRAFT_ECS_PROJECTILE_DEFINITIONS_H
#define MECRAFT_ECS_PROJECTILE_DEFINITIONS_H

#include "../components/ProjectileComponents.h"
#include "../../item/Item.h"
#include "../../world/block/Block.h"

#include <string>

namespace ecs {

BlockID defaultProjectileEntityImpactParticleBlock();
ProjectileDefinition makeAppleProjectileDefinition();
bool ensureThrowableProjectileDefinitionsLoaded(std::string* error = nullptr);
bool getThrowableProjectileDefinition(ItemID itemId, ProjectileDefinition& outDefinition);
ProjectileDefinition projectileDefinitionForItemOrDefault(ItemID itemId);

} // namespace ecs

#endif // MECRAFT_ECS_PROJECTILE_DEFINITIONS_H
