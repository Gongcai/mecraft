#ifndef MECRAFT_ECS_PARTICLE_SPAWN_SYSTEM_H
#define MECRAFT_ECS_PARTICLE_SPAWN_SYSTEM_H

#include "../../ISystem.h"
#include "../../components/Components.h"

namespace ecs {

class ParticleSpawnSystem : public ISystem {
public:
    using Dependencies = SystemDependency<
        std::tuple<ParticleTag, ParticleComponent>,
        std::tuple<ParticleTag, TransformComponent, VelocityComponent, ParticleComponent>
    >;

    void update(SystemContext& ctx) override;
};

} // namespace ecs

#endif // MECRAFT_ECS_PARTICLE_SPAWN_SYSTEM_H
