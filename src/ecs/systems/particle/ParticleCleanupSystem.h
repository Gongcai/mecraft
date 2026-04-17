#ifndef MECRAFT_ECS_PARTICLE_CLEANUP_SYSTEM_H
#define MECRAFT_ECS_PARTICLE_CLEANUP_SYSTEM_H

#include "../../GameplayRegistry.h"

namespace ecs {

class ParticleCleanupSystem {
public:
    static void update(GameplayRegistry& registry);
};

} // namespace ecs

#endif // MECRAFT_ECS_PARTICLE_CLEANUP_SYSTEM_H
