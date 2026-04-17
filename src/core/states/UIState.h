#ifndef MECRAFT_UISTATE_H
#define MECRAFT_UISTATE_H

#include "../IGameState.h"
#include "../GameStateMachine.h"
#include "../InputContextManager.h"
#include "../Time.h"
#include "StateDependencies.h"

class UIState : public IGameState {
public:
    explicit UIState(StateDependencies deps)
            : m_deps(deps) {}

    void onEnter() override {
        m_deps.context.pushContext(InputContextType::UI);
        m_deps.input.captureMouse(false);
    }

    void onExit() override {
        m_deps.context.popContext();
        if (m_deps.context.getCurrentContext() == InputContextType::Gameplay) {
             m_deps.input.captureMouse(true);
        }
    }

    void update(float dt, const InputSnapshot& snapshot) override {
        static_cast<void>(dt);
        static_cast<void>(snapshot);
        if (m_deps.context.isActionTriggered(Action::Menu) ||
            m_deps.context.isActionTriggered(Action::Cancel)) {
            m_deps.fsm.popState();
            return;
        }
    }

private:
    StateDependencies m_deps;
};

#endif //MECRAFT_UISTATE_H
