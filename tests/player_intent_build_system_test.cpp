#include <cmath>
#include <cstdlib>
#include <iostream>

#include "../src/ecs/GameplayRegistry.h"
#include "../src/ecs/SystemContext.h"
#include "../src/ecs/components/Components.h"
#include "../src/ecs/systems/player/PlayerIntentBuildSystem.h"
#include "../src/ecs/util/InputFrameState.h"

namespace {

int fail(const char* message) {
    std::cerr << "[player_intent_build_system_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

bool nearlyEqual(const float a, const float b, const float epsilon = 0.001f) {
    return std::abs(a - b) <= epsilon;
}

} // namespace

int main() {
    ecs::GameplayRegistry registry;
    ecs::GameplayServices services;
    ecs::SystemContext ctx{registry, services};
    ecs::PlayerIntentBuildSystem system;
    auto& frame = registry.ctxSet<ecs::InputFrameState>();

    const auto entity = registry.create();
    registry.emplace<ecs::LocalPlayerTag>(entity);
    registry.emplace<ecs::MoveIntentComponent>(entity);
    registry.emplace<ecs::LookIntentComponent>(entity);
    registry.emplace<ecs::HotbarIntentComponent>(entity);
    registry.emplace<ecs::BlockActionIntentComponent>(entity);
    auto& camera = registry.emplace<ecs::CameraStateComponent>(entity);

    camera.front = glm::vec3(0.0f, 0.0f, -1.0f);
    camera.right = glm::vec3(1.0f, 0.0f, 0.0f);

    frame.verticalAxis = 1.0f;
    frame.jump = true;
    frame.jumpDoubleTap = true;
    frame.sprint = true;
    frame.crouch = true;
    frame.lookX = 3.5f;
    frame.lookY = -1.25f;
    frame.attack = true;
    frame.useItem = true;
    frame.hotbar[2] = true;
    frame.hotbarScrollDown = true;
    frame.gameplayContextActive = true;

    system.update(ctx);

    const auto& move = registry.get<ecs::MoveIntentComponent>(entity);
    const auto& look = registry.get<ecs::LookIntentComponent>(entity);
    const auto& hotbar = registry.get<ecs::HotbarIntentComponent>(entity);
    const auto& block = registry.get<ecs::BlockActionIntentComponent>(entity);

    if (!nearlyEqual(move.move.x, 0.0f) || !nearlyEqual(move.move.y, -1.0f)) {
        return fail("forward input should be converted into world-space movement");
    }
    if (!move.wantsJump || !move.wantsSprint || !move.wantsCrouch) {
        return fail("basic movement actions should propagate into move intent");
    }
    if (!move.toggleFlightMode) {
        return fail("double-tap jump should request a flight mode toggle");
    }
    if (!nearlyEqual(look.deltaX, 3.5f) || !nearlyEqual(look.deltaY, -1.25f)) {
        return fail("look deltas should propagate into look intent");
    }
    if (!hotbar.slotSelected[2] || !hotbar.scrollDown) {
        return fail("hotbar actions should propagate into hotbar intent");
    }
    if (!block.wantsBreak || !block.wantsPlace) {
        return fail("block actions should propagate into block intent");
    }

    frame = {};
    frame.gameplayContextActive = false;
    system.update(ctx);

    const auto& clearedMove = registry.get<ecs::MoveIntentComponent>(entity);
    if (clearedMove.toggleFlightMode || clearedMove.wantsJump || clearedMove.wantsCrouch || clearedMove.wantsSprint) {
        return fail("move intent should clear when gameplay context is inactive");
    }

    std::cout << "[player_intent_build_system_test] PASS\n";
    return EXIT_SUCCESS;
}
