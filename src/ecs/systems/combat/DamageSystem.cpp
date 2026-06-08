#include "DamageSystem.h"

#include <algorithm>

#include "../../util/DamageEventBuffer.h"
#include "../../util/GameplayRuntimeContext.h"

namespace ecs {
namespace {

bool isCreativeLocalPlayer(GameplayRegistry& registry, const entt::entity target) {
    if (!registry.has<LocalPlayerTag>(target) || !registry.ctxHas<GameplayRuntimeContext>()) {
        return false;
    }
    return registry.ctxGet<GameplayRuntimeContext>().gameplayMode == GameplayMode::Creative;
}

} // namespace

void DamageSystem::update(SystemContext& ctx) {
    auto& registry = ctx.registry;
    if (!registry.ctxHas<DamageEventBus>()) {
        return;
    }

    auto& damageBus = registry.ctxGet<DamageEventBus>();
    auto& reg = registry.registry();

    for (const DamageEvent& event : damageBus.events) {
        if (event.amount <= 0 || event.target == entt::null || !reg.valid(event.target)) {
            continue;
        }
        if (isCreativeLocalPlayer(registry, event.target)) {
            continue;
        }

        auto* health = reg.try_get<HealthComponent>(event.target);
        if (health == nullptr || health->current <= 0) {
            continue;
        }

        health->current = std::max(0, health->current - event.amount);
        if (auto* hurt = reg.try_get<HurtEffectComponent>(event.target)) {
            hurt->classicHurtEffectPending = true;
        }
    }

    damageBus.clear();
}

} // namespace ecs
