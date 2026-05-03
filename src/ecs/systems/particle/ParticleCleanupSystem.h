#ifndef MECRAFT_ECS_PARTICLE_CLEANUP_SYSTEM_H
#define MECRAFT_ECS_PARTICLE_CLEANUP_SYSTEM_H

#include "../../ISystem.h"
#include "../../components/Components.h"

namespace ecs {

class ParticleCleanupSystem : public ISystem {
public:
    using Dependencies = SystemDependency<
        std::tuple<ParticleTag, ParticleComponent>,
        std::tuple<>
    >;

    void update(SystemContext& ctx) override;
};

} // namespace ecs

#endif // MECRAFT_ECS_PARTICLE_CLEANUP_SYSTEM_H
