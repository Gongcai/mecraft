#include "PlayerIntentBuildSystem.h"

#include "../../util/InputFrameState.h"
#include "../../components/Components.h"

#include <glm/glm.hpp>

namespace ecs {

void PlayerIntentBuildSystem::update(SystemContext& ctx) {
    auto& registry = ctx.registry;
    const InputFrameState& frame = registry.ctxGet<InputFrameState>();

    auto view = registry.view<LocalPlayerTag, MoveIntentComponent, LookIntentComponent,
                              HotbarIntentComponent, BlockActionIntentComponent, CameraStateComponent>();

    if (!frame.gameplayContextActive) {
        for (auto e : view) {
            auto& move = view.get<MoveIntentComponent>(e);
            auto& look = view.get<LookIntentComponent>(e);
            auto& hotbar = view.get<HotbarIntentComponent>(e);
            auto& block = view.get<BlockActionIntentComponent>(e);

            move = {};
            look = {};
            hotbar = {};
            block = {};
        }
        return;
    }

    for (auto e : view) {
        if (const auto* health = registry.registry().try_get<HealthComponent>(e);
            health != nullptr && health->current <= 0) {
            auto& move = view.get<MoveIntentComponent>(e);
            auto& look = view.get<LookIntentComponent>(e);
            auto& hotbar = view.get<HotbarIntentComponent>(e);
            auto& block = view.get<BlockActionIntentComponent>(e);

            move = {};
            look = {};
            hotbar = {};
            block = {};
            continue;
        }

        const auto& camera = view.get<CameraStateComponent>(e);
        auto& move = view.get<MoveIntentComponent>(e);
        auto& look = view.get<LookIntentComponent>(e);
        auto& hotbar = view.get<HotbarIntentComponent>(e);
        auto& block = view.get<BlockActionIntentComponent>(e);

        glm::vec3 front = camera.front;
        glm::vec3 right = camera.right;
        front.y = 0.0f;
        right.y = 0.0f;
        if (glm::length(front) > 0.001f) {
            front = glm::normalize(front);
        }
        if (glm::length(right) > 0.001f) {
            right = glm::normalize(right);
        }

        glm::vec3 wishDir = front * frame.verticalAxis + right * frame.horizontalAxis;
        if (glm::length(wishDir) > 0.001f) {
            wishDir = glm::normalize(wishDir);
        }

        move.move = glm::vec2(wishDir.x, wishDir.z);
        move.wantsJump = frame.jump;
        move.wantsSprint = frame.sprint;
        move.wantsCrouch = frame.crouch;
        move.toggleFlightMode = frame.jumpDoubleTap;

        look.deltaX = frame.lookX;
        look.deltaY = frame.lookY;

        for (int i = 0; i < 9; ++i) {
            hotbar.slotSelected[i] = frame.hotbar[i];
        }
        hotbar.scrollUp = frame.hotbarScrollUp;
        hotbar.scrollDown = frame.hotbarScrollDown;

        block.wantsBreak = frame.attack;
        block.wantsPlace = frame.useItem;
    }
}

} // namespace ecs
