#include "ParticleSimulationSystem.h"

#include "../../components/Components.h"
#include "../../util/ParticlePhysicsHelpers.h"
#include "../../../world/World.h"

namespace ecs {

void ParticleSimulationSystem::update(SystemContext& ctx) {
    auto& registry = ctx.registry;
    const float dt = ctx.dt;

    if (dt <= 0.0f || !ctx.services.world) {
        return;
    }
    const World& world = *ctx.services.world;

    auto view = registry.view<ParticleTag, TransformComponent, VelocityComponent, ParticleComponent>();
    for (const entt::entity e : view) {
        auto& transform = view.get<TransformComponent>(e);
        auto& velocity = view.get<VelocityComponent>(e);
        auto& particle = view.get<ParticleComponent>(e);

        particle.life -= dt;
        if (particle.life <= 0.0f) {
            continue;
        }

        particle_detail::simulateParticleStep(transform, velocity, particle, world, dt);
    }
}

} // namespace ecs
