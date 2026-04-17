#ifndef MECRAFT_ECS_PARTICLE_SPAWN_SYSTEM_H
#define MECRAFT_ECS_PARTICLE_SPAWN_SYSTEM_H

#include "../../GameplayRegistry.h"

namespace ecs {

class ParticleSpawnSystem {
public:
    static void update(GameplayRegistry& registry);
};

} // namespace ecs

#endif // MECRAFT_ECS_PARTICLE_SPAWN_SYSTEM_H
