#include "PlayerAudioBridgeSystem.h"

#include <string>

#include "../../util/AudioEventBuffer.h"
#include "../../components/Components.h"
#include "../../../player/Player.h"

namespace ecs {

namespace {
constexpr float kMinFallSoundImpactSpeed = 6.0f;
constexpr float kBigFallImpactSpeed = 10.0f;
}

void PlayerAudioBridgeSystem::update(GameplayRegistry& registry, Player& player, const float dt) {
    auto& audioEvents = ensureAudioEventBuffer(registry);
    auto view = registry.view<LocalPlayerTag, FootstepStateComponent, LandingStateComponent>();
    for (auto e : view) {
        auto& footstep = view.get<FootstepStateComponent>(e);
        const auto& landing = view.get<LandingStateComponent>(e);

        // if (!registry.has<AudioSourceComponent>(e)) {
        //     auto& source = registry.emplace<AudioSourceComponent>(e);
        //     source.clipName = "walk_grass";
        //     source.loop = true;
        //     source.volume = 0.12f;
        //     source.pitch = 0.85f;
        //     source.spatial = false;
        //     source.referenceDistance = 8.0f;
        //     source.rolloff = 0.0f;
        //     source.desiredPlaying = false;
        //     source.followTransform = false;
        // }
        // auto& underwaterLoop = registry.get<AudioSourceComponent>(e);
        // underwaterLoop.desiredPlaying = player.isFullySubmerged();

        if (player.isMoving()) {
            const float stepInterval = player.isSprinting() ? 0.35f : 0.5f;
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
        audioEvents.playSoundEvents.push_back({clipName, player.getPosition(), true, 1.0f});
        if (isBigFall) {
            player.triggerClassicHurtEffect();
        }
    }
}

} // namespace ecs
