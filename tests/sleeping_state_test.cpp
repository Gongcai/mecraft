#include <cstdlib>
#include <iostream>
#include <memory>

#include "../src/engine/input/InputContextManager.h"
#include "../src/game/states/GameStateMachine.h"
#include "../src/game/states/SleepingState.h"
#include "../src/player/ActionMap.h"
#include "../src/world/World.h"

namespace {

int fail(const char* message) {
    std::cerr << "[sleeping_state_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

void pushSleepingState(GameStateMachine& fsm, InputContextManager& context, InputManager& input, World& world) {
    fsm.pushState(std::make_unique<SleepingState>(fsm, context, input, world));
}

} // namespace

int main() {
    ActionMap actionMap;
    InputManager input;
    InputContextManager context(actionMap, input);
    World world;
    constexpr float kFixedStep = 1.0f / 60.0f;

    GameStateMachine automaticWakeFsm;
    world.getDayNightSystem().setTimeOfDay(900.0f);
    pushSleepingState(automaticWakeFsm, context, input, world);
    if (!automaticWakeFsm.pausesSimulation()) {
        return fail("sleeping state should pause simulation");
    }
    if (context.getCurrentContext() != InputContextType::UI) {
        return fail("sleeping state should switch to UI input context");
    }

    InputSnapshot emptySnapshot;
    for (int i = 0; i < 300 && !automaticWakeFsm.isEmpty(); ++i) {
        automaticWakeFsm.update(kFixedStep, emptySnapshot);
    }
    if (!automaticWakeFsm.isEmpty()) {
        return fail("sleeping state should exit after reaching daytime");
    }
    if (!world.getDayNightSystem().isFullDaytime()) {
        return fail("sleeping state should advance time until full daytime");
    }
    if (context.getCurrentContext() != InputContextType::Gameplay) {
        return fail("sleeping state should restore gameplay input context after automatic wake");
    }

    GameStateMachine manualWakeFsm;
    world.getDayNightSystem().setTimeOfDay(900.0f);
    pushSleepingState(manualWakeFsm, context, input, world);
    InputSnapshot wakeSnapshot;
    wakeSnapshot.keys[GLFW_KEY_SPACE] = true;
    wakeSnapshot.keysJustPressed[GLFW_KEY_SPACE] = true;
    manualWakeFsm.update(kFixedStep, wakeSnapshot);
    if (manualWakeFsm.isEmpty()) {
        return fail("manual wake input should not pop the sleeping state in the same frame");
    }
    if (!manualWakeFsm.pausesSimulation()) {
        return fail("manual wake input should keep simulation paused for the wake frame");
    }

    manualWakeFsm.update(kFixedStep, emptySnapshot);
    if (!manualWakeFsm.isEmpty()) {
        return fail("manual wake input should exit sleeping state on the next update");
    }
    if (context.getCurrentContext() != InputContextType::Gameplay) {
        return fail("sleeping state should restore gameplay input context after manual wake");
    }

    std::cout << "[sleeping_state_test] PASS\n";
    return EXIT_SUCCESS;
}
