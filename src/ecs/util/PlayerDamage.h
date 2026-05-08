#ifndef MECRAFT_ECS_PLAYER_DAMAGE_H
#define MECRAFT_ECS_PLAYER_DAMAGE_H

#include <algorithm>
#include "../GameplayRegistry.h"
#include "../util/GameplayRuntimeContext.h"
#include "../components/Components.h"

namespace ecs {

/// Apply damage to the local player. Reduces health, triggers hurt effect.
/// Does nothing if amount <= 0 or if the player is in creative mode.
inline void applyDamage(GameplayRegistry& registry, int amount) {
    if (amount <= 0) return;

    if (registry.ctxHas<GameplayRuntimeContext>()) {
        if (registry.ctxGet<GameplayRuntimeContext>().gameplayMode == GameplayMode::Creative) {
            return;
        }
    }

    auto view = registry.view<LocalPlayerTag, HealthComponent, HurtEffectComponent>();
    for (auto e : view) {
        auto& health = view.get<HealthComponent>(e);
        auto& hurt = view.get<HurtEffectComponent>(e);

        health.current = std::max(0, health.current - amount);
        hurt.classicHurtEffectPending = true;
    }
}

} // namespace ecs

#endif // MECRAFT_ECS_PLAYER_DAMAGE_H
