#include "HurtEffectDecaySystem.h"

#include <algorithm>

namespace ecs {

void HurtEffectDecaySystem::update(SystemContext& ctx) {
    if (ctx.dt <= 0.0f) {
        return;
    }

    auto view = ctx.registry.registry().view<HurtEffectComponent>();
    for (const entt::entity entity : view) {
        auto& hurt = view.get<HurtEffectComponent>(entity);
        hurt.flashSecondsRemaining = std::max(0.0f, hurt.flashSecondsRemaining - ctx.dt);
        if (hurt.flashSecondsRemaining <= 0.0f) {
            hurt.classicHurtEffectPending = false;
        }
    }
}

} // namespace ecs
