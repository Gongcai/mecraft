#include "PlayerAudioBridgeSystem.h"

#include <string>

#include "../../util/AudioEventBuffer.h"
#include "../../components/Components.h"
#include "../../util/PlayerQuery.h"

namespace ecs {

namespace {
constexpr float kMinFallSoundImpactSpeed = 6.0f;
constexpr float kBigFallImpactSpeed = 10.0f;
}

void PlayerAudioBridgeSystem::update(GameplayRegistry& registry, const float dt) {
    auto& audioEvents = ensureAudioEventBuffer(registry);
    PlayerQuery query(registry);

    auto view = registry.view<LocalPlayerTag, FootstepStateComponent, LandingStateComponent>();
    for (auto e : view) {
        auto& footstep = view.get<FootstepStateComponent>(e);
        const auto& landing = view.get<LandingStateComponent>(e);

        if (query.isMoving()) {
            const float stepInterval = query.isSprinting() ? 0.35f : 0.5f;
            footstep.timer -= dt;
            if (footstep.timer <= 0.0f) {
                const std::string soundName = "walk_grass" + std::to_string(footstep.clipIndex + 1);
                audioEvents.playSoundEvents.push_back({soundName, glm::vec3(0.0f), false, 1.0f});
                footstep.clipIndex = (footstep.clipIndex + 1) % 6;
                footstep.timer = stepInterval;
            }
        }

        if (!landing.justLanded) {
            continue;
        }

        const float impactSpeed = landing.impactSpeed;
        if (impactSpeed < kMinFallSoundImpactSpeed) {
            continue;
        }

        const bool isBigFall = impactSpeed >= kBigFallImpactSpeed;
        const char* clipName = isBigFall ? "classic-hurt" : "fallsmall";
        audioEvents.playSoundEvents.push_back({clipName, query.getPosition(), true, 1.0f});
        if (isBigFall) {
            // Trigger classic hurt effect via ECS component
            auto hurtView = registry.view<LocalPlayerTag, HurtEffectComponent>();
            for (auto he : hurtView) {
                hurtView.get<HurtEffectComponent>(he).classicHurtEffectPending = true;
            }
        }
    }
}

} // namespace ecs
