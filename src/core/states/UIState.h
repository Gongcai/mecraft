#ifndef MECRAFT_UISTATE_H
#define MECRAFT_UISTATE_H

#include "../IGameState.h"
#include "../GameStateMachine.h"
#include "../InputContextManager.h"
#include "StateDependencies.h"
#include "../../ui/UIInputAdapter.h"
#include "../../ui/UIRenderer.h"
#include "../../ui/screens/PauseMenuScreen.h"
#include "../../locale/LocaleManager.h"

class UIState : public IGameState {
public:
    explicit UIState(StateDependencies deps)
            : m_deps(deps) {}

    void onEnter() override {
        m_deps.context.pushContext(InputContextType::UI);
        m_deps.input.captureMouse(false);

        ResourceMgr* rm = m_deps.uiRenderer.getResourceMgr();
        if (rm) {
            m_pauseScreen.setLocaleManager(&m_deps.localeManager);
            m_pauseScreen.init(*rm);
            m_pauseScreen.onResume = [this]() {
                m_deps.fsm.popState();
            };
            m_pauseScreen.onQuitToMenu = [this]() {
                m_quitToMenu = true;
            };
            m_deps.uiRenderer.setActiveScene(&m_pauseScreen);
            m_pauseScreen.enterScene();
        }
    }

    void onExit() override {
        m_deps.uiRenderer.setActiveScene(nullptr);
        m_pauseScreen.shutdown();
        m_deps.context.popContext();
        if (m_deps.context.getCurrentContext() == InputContextType::Gameplay) {
            m_deps.input.captureMouse(true);
        }
    }

    void update(float dt, const InputSnapshot& snapshot) override {
        const UIInputRouteResult uiRouteResult =
            UIInputAdapter::routeInput(m_deps.uiRenderer, snapshot, m_deps.context);

        const bool cancel = m_deps.context.isActionTriggered(Action::Cancel);
        const bool menu = m_deps.context.isActionTriggered(Action::Menu);

        m_pauseScreen.updateAnimations(dt);

        if ((menu || cancel) && uiRouteResult.aggregate != UIEventResult::Consumed) {
            m_deps.fsm.popState();
            return;
        }

        if (m_quitToMenu) {
            m_quitToMenu = false;
            // Pop this state first, then signal quit to the game
            m_deps.fsm.popState();
            // The quit-to-menu signal is handled by GameplayAppState
            // via the GameStateMachine's state change
            m_deps.fsm.requestQuitToMenu();
        }
    }

private:
    StateDependencies m_deps;
    PauseMenuScreen m_pauseScreen;
    bool m_quitToMenu = false;
};

#endif //MECRAFT_UISTATE_H
