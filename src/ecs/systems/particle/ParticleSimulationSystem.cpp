#include "ParticleSimulationSystem.h"

#include "../../components/Components.h"

namespace ecs {

namespace {
constexpr float kGravity = -14.0f;
}

void ParticleSimulationSystem::update(GameplayRegistry& registry, const float dt) {
    if (dt <= 0.0f) {
        return;
    }

    auto view = registry.view<ParticleTag, TransformComponent, VelocityComponent, ParticleComponent>();
    for (const entt::entity e : view) {
        auto& transform = view.get<TransformComponent>(e);
        auto& velocity = view.get<VelocityComponent>(e);
        auto& particle = view.get<ParticleComponent>(e);

        particle.life -= dt;
        if (particle.life <= 0.0f) {
            continue;
        }

        velocity.velocity.y += kGravity * dt;
        transform.position += velocity.velocity * dt;
    }
}

} // namespace ecs
