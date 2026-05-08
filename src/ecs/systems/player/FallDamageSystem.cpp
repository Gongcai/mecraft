#include "FallDamageSystem.h"

#include "../../components/Components.h"
#include "../../util/PlayerDamage.h"

namespace ecs {

namespace {
constexpr float kSafeImpactSpeed = 10.0f;  // no damage below this speed (~3 blocks fall)
} // namespace

void FallDamageSystem::update(SystemContext& ctx) {
    auto& registry = ctx.registry;

    auto view = registry.view<LocalPlayerTag, LandingStateComponent>();
    for (auto e : view) {
        const auto& landing = view.get<LandingStateComponent>(e);

        if (!landing.justLanded || landing.impactSpeed <= kSafeImpactSpeed) {
            continue;
        }

        const int damage = static_cast<int>(landing.impactSpeed - kSafeImpactSpeed);
        applyDamage(registry, damage);
    }
}

} // namespace ecs
