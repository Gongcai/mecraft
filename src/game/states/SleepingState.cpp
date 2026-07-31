#include "SleepingState.h"

#include "GameStateMachine.h"
#include "engine/input/InputContextManager.h"
#include "engine/input/InputManager.h"
#include "../../world/World.h"

namespace {
constexpr float kSleepTimeScale = 120.0f;
}

SleepingState::SleepingState(GameStateMachine& fsm, InputContextManager& context, InputManager& input, World& world)
    : m_fsm(fsm), m_context(context), m_input(input), m_world(world) {}

void SleepingState::onEnter() {
    m_context.pushContext(InputContextType::UI);
    m_input.captureMouse(false);
}

void SleepingState::onExit() {
    m_context.popContext();
    if (m_context.getCurrentContext() == InputContextType::Gameplay) {
        m_input.captureMouse(true);
    }
}

void SleepingState::update(const float dt, const InputSnapshot& snapshot) {
    if (m_wakePending) {
        m_fsm.popState();
        return;
    }

    if (hasManualWakeInput(snapshot)) {
        m_wakePending = true;
        return;
    }

    auto& dayNight = m_world.getDayNightSystem();
    if (dayNight.isFullDaytime()) {
        m_wakePending = true;
        return;
    }

    dayNight.update(dt * kSleepTimeScale);
    if (dayNight.isFullDaytime()) {
        m_wakePending = true;
    }
}

bool SleepingState::hasManualWakeInput(const InputSnapshot& snapshot) {
    for (int key = 0; key <= GLFW_KEY_LAST; ++key) {
        if (snapshot.keysJustPressed[key]) {
            return true;
        }
    }
    for (int button = 0; button <= GLFW_MOUSE_BUTTON_LAST; ++button) {
        if (snapshot.mouseButtonsJustPressed[button]) {
            return true;
        }
    }
    if (snapshot.scrollDelta != 0.0 || snapshot.typedCharCount > 0) {
        return true;
    }
    if (!snapshot.gamepad.connected) {
        return false;
    }
    for (int button = 0; button <= GLFW_GAMEPAD_BUTTON_LAST; ++button) {
        if (snapshot.gamepad.buttonsJustPressed[button]) {
            return true;
        }
    }
    return false;
}
