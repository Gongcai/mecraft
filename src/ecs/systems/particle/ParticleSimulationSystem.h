#ifndef MECRAFT_ECS_PARTICLE_SIMULATION_SYSTEM_H
#define MECRAFT_ECS_PARTICLE_SIMULATION_SYSTEM_H

#include "../../ISystem.h"

namespace ecs {

class ParticleSimulationSystem : public ISystem {
public:
    void update(SystemContext& ctx) override;
};

} // namespace ecs

#endif // MECRAFT_ECS_PARTICLE_SIMULATION_SYSTEM_H
