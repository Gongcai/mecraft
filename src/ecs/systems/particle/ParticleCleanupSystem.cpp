#include "ParticleCleanupSystem.h"

#include <vector>

#include "../../components/Components.h"

namespace ecs {

void ParticleCleanupSystem::update(GameplayRegistry& registry) {
    auto view = registry.view<ParticleTag, ParticleComponent>();
    std::vector<entt::entity> removed;
    for (const entt::entity e : view) {
        if (view.get<ParticleComponent>(e).life <= 0.0f) {
            removed.push_back(e);
        }
    }

    for (const entt::entity e : removed) {
        if (registry.registry().valid(e)) {
            registry.destroy(e);
        }
    }
}

} // namespace ecs
