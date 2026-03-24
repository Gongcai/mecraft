#ifndef MECRAFT_UISTATE_H
#define MECRAFT_UISTATE_H

#include "../IGameState.h"
#include "../GameStateMachine.h"
#include "../InputContextManager.h"
#include "../Time.h"

class UIState : public IGameState {
public:
    UIState(GameStateMachine& fsm,
            InputContextManager& ctx,
            InputManager& input)
            : m_fsm(fsm), m_context(ctx), m_input(input) {}

    void onEnter() override {
        // Activate UI context
        m_context.pushContext(InputContextType::UI);
        // Release mouse for UI interaction
        m_input.captureMouse(false);
        // pause game time
        Time::setTimeSpeed(0.0f);
    }

    void onExit() override {
        // Deactivate UI context
        m_context.popContext();

        // Restore mouse capture if verify we are back to Gameplay
        // A robust way is checking the context below, or letting GameplayState::onResume handle it.
        // For simple stack, we can check current context:
        if (m_context.getCurrentContext() == InputContextType::Gameplay) {
             m_input.captureMouse(true);
        }
        // restore game time
        Time::setTimeSpeed(1.0f);
    }

    void update(float dt, const InputSnapshot& snapshot) override {
        // Check for exit conditions
        if (m_context.isActionTriggered(Action::Menu) ||
            m_context.isActionTriggered(Action::Cancel)) {
            m_fsm.popState();
            return;
        }

        // Logic for UI update goes here
    }

private:
    GameStateMachine& m_fsm;
    InputContextManager& m_context;
    InputManager& m_input;
};

#endif //MECRAFT_UISTATE_H

