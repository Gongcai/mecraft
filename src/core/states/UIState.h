#ifndef MECRAFT_UISTATE_H
#define MECRAFT_UISTATE_H

#include "../IGameState.h"
#include "../GameStateMachine.h"
#include "../InputContextManager.h"
#include "StateDependencies.h"
#include "../../ui/UIRenderer.h"
#include "../../ui/UIInputEvent.h"
#include "../../ui/screens/PauseMenuScreen.h"
#include "../../locale/LocaleManager.h"
#include <GLFW/glfw3.h>

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
        // Route UI input to the pause screen
        const float mx = snapshot.mousePosition.x;
        const float my = snapshot.mousePosition.y;
        auto routeKeyPress = [this, mx, my](int keyCode) {
            static_cast<void>(m_deps.uiRenderer.routeUIInput({UIInputEventType::KeyDown, mx, my, UIPointerButton::None, keyCode}));
            static_cast<void>(m_deps.uiRenderer.routeUIInput({UIInputEventType::KeyUp, mx, my, UIPointerButton::None, keyCode}));
        };
        auto routeKeyDown = [this, mx, my](int keyCode) {
            static_cast<void>(m_deps.uiRenderer.routeUIInput({UIInputEventType::KeyDown, mx, my, UIPointerButton::None, keyCode}));
        };

        static_cast<void>(m_deps.uiRenderer.routeUIInput({UIInputEventType::PointerMove, mx, my, UIPointerButton::None}));

        if (snapshot.mouseButtonsJustPressed[GLFW_MOUSE_BUTTON_LEFT]) {
            static_cast<void>(m_deps.uiRenderer.routeUIInput({UIInputEventType::PointerDown, mx, my, UIPointerButton::Primary}));
        }
        if (snapshot.mouseButtonsJustReleased[GLFW_MOUSE_BUTTON_LEFT]) {
            static_cast<void>(m_deps.uiRenderer.routeUIInput({UIInputEventType::PointerUp, mx, my, UIPointerButton::Primary}));
        }

        const bool navUp = m_deps.context.isActionTriggered(Action::Up);
        const bool navDown = m_deps.context.isActionTriggered(Action::Down);
        const bool navLeft = m_deps.context.isActionTriggered(Action::Left);
        const bool navRight = m_deps.context.isActionTriggered(Action::Right);
        const bool confirm = m_deps.context.isActionTriggered(Action::Confirm);
        const bool cancel = m_deps.context.isActionTriggered(Action::Cancel);
        const bool menu = m_deps.context.isActionTriggered(Action::Menu);

        if (navUp) {
            routeKeyDown(GLFW_KEY_UP);
        }
        if (navDown) {
            routeKeyDown(GLFW_KEY_DOWN);
        }
        if (navLeft) {
            routeKeyDown(GLFW_KEY_LEFT);
        } else if (navRight) {
            routeKeyDown(GLFW_KEY_RIGHT);
        }
        if (confirm) {
            routeKeyPress(GLFW_KEY_ENTER);
        }

        m_pauseScreen.updateAnimations(dt);

        if (menu || cancel) {
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
