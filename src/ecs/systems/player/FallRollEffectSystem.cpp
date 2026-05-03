#include "FallRollEffectSystem.h"

#include <algorithm>

#include "../../components/Components.h"
#include "../../../player/Player.h"

namespace ecs {

void FallRollEffectSystem::update(GameplayRegistry& registry, Player& player, const float dt) {
    auto view = registry.view<LocalPlayerTag, FallRollComponent>();
    for (auto e : view) {
        auto& roll = view.get<FallRollComponent>(e);

        // Trigger the roll when the player has a pending classic hurt effect.
        if (player.consumeClassicHurtEffect()) {
            roll.active = true;
            roll.elapsed = 0.0f;
        }

        if (!roll.active) continue;

        roll.elapsed += dt;
        float t = roll.elapsed / FallRollComponent::kDurationSeconds;
        t = std::clamp(t, 0.0f, 1.0f);

        if (t < FallRollComponent::kPeakRatio) {
            const float phase = t / FallRollComponent::kPeakRatio;
            roll.currentRadians = -FallRollComponent::kMaxRadians * phase;
        } else {
            const float phase = (t - FallRollComponent::kPeakRatio)
                                / (1.0f - FallRollComponent::kPeakRatio);
            roll.currentRadians = -FallRollComponent::kMaxRadians * (1.0f - phase);
        }

        if (t >= 1.0f) {
            roll.active = false;
            roll.currentRadians = 0.0f;
        }
    }
}

} // namespace ecs
