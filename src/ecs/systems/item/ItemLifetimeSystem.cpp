#include "ItemLifetimeSystem.h"

#include <vector>

#include "../../components/Components.h"
#include "../../util/SimulationDistance.h"

namespace ecs {

void ItemLifetimeSystem::update(SystemContext& ctx) {
    auto& registry = ctx.registry;
    const float dt = ctx.dt;

    if (dt <= 0.0f) {
        return;
    }

    auto view = registry.view<DropItemTag, LifetimeComponent>();
    std::vector<entt::entity> removed;

    for (const entt::entity e : view) {
        if (!simulation::isEntityTicking(ctx, e)) {
            continue;
        }

        auto& lifetime = view.get<LifetimeComponent>(e);
        lifetime.ageSeconds += dt;
        if (lifetime.ageSeconds >= lifetime.lifeTimeSeconds) {
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
