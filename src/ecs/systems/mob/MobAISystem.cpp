#include "MobAISystem.h"
#include "../../GameplayRegistry.h"
#include "../../components/Components.h"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <cstdlib>
#include <cmath>

namespace ecs {

void MobAISystem::update(GameplayRegistry& registry, float dt) {
    if (dt <= 0.0f) return;

    auto& reg = registry.registry();
    auto view = reg.view<MobTag, MobAIComponent, MoveIntentComponent>();

    for (auto entity : view) {
        auto& ai = view.get<MobAIComponent>(entity);
        auto& moveIntent = view.get<MoveIntentComponent>(entity);

        ai.wanderTimer -= dt;

        if (ai.wanderTimer <= 0.0f) {
            ai.wanderTimer = ai.wanderInterval * (0.8f + 0.4f * (std::rand() / (float)RAND_MAX));

            // 30% chance to stop and idle
            if ((std::rand() % 100) < 30) {
                ai.wanderDir = {0.0f, 0.0f};
            } else {
                float angle = (std::rand() / (float)RAND_MAX) * glm::two_pi<float>();
                ai.wanderDir = {std::cos(angle), std::sin(angle)};
                ai.yaw = glm::degrees(angle) - 90.0f;
            } 
        }

        moveIntent.move = ai.wanderDir * ai.wanderSpeed;
        moveIntent.wantsJump = false;
        moveIntent.wantsSprint = false;
        moveIntent.wantsCrouch = false;
        moveIntent.toggleFlightMode = false;
    }
}

} // namespace ecs
