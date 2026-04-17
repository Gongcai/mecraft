#ifndef MECRAFT_ECS_PARTICLE_SIMULATION_SYSTEM_H
#define MECRAFT_ECS_PARTICLE_SIMULATION_SYSTEM_H

#include "../../GameplayRegistry.h"

namespace ecs {

class ParticleSimulationSystem {
public:
    static void update(GameplayRegistry& registry, float dt);
};

} // namespace ecs

#endif // MECRAFT_ECS_PARTICLE_SIMULATION_SYSTEM_H
