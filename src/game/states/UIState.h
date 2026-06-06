#ifndef MECRAFT_UISTATE_H
#define MECRAFT_UISTATE_H

#include "IGameState.h"
#include "UIStateContext.h"
#include "../../ui/core/UIInputAdapter.h"
#include "../../ui/screens/PauseMenuScreen.h"
#include "../../ui/screens/SettingsScreen.h"
#include "engine/input/InputContextManager.h"
#include "ui/core/UIRenderer.h"

/// G6: UIState uses UIStateContext — only UI-relevant dependencies.
/// Manages both the pause menu and the settings screen (sub-screen approach).
class UIState : public IGameState {
public:
    explicit UIState(UIStateContext ctx)
            : m_ctx(ctx) {}

    void onEnter() override {
        m_ctx.context.pushContext(InputContextType::UI);
        m_ctx.input.captureMouse(false);

        ResourceMgr* rm = m_ctx.uiRenderer.getResourceMgr();
        if (rm) {
            // -- Pause menu --
            m_pauseScreen.setLocaleManager(&m_ctx.localeManager);
            m_pauseScreen.init(*rm);
            m_pauseScreen.onResume = [this]() {
                m_ctx.fsm.popState();
            };
            m_pauseScreen.onSettings = [this]() {
                switchToSettings();
            };
            m_pauseScreen.onQuitToMenu = [this]() {
                m_quitToMenu = true;
            };

            // -- Settings screen --
            m_settingsScreen.setLocaleManager(&m_ctx.localeManager);
            m_settingsScreen.setRenderScene(m_ctx.renderScene);
            m_settingsScreen.setWorld(m_ctx.world);
            m_settingsScreen.init(*rm);
            m_settingsScreen.onBack = [this]() {
                switchToPause();
            };

            // Start with pause menu
            m_showingSettings = false;
            m_ctx.uiRenderer.setActiveScene(&m_pauseScreen);
            m_pauseScreen.enterScene();
        }
    }

    void onExit() override {
        m_ctx.uiRenderer.setActiveScene(nullptr);
        m_settingsScreen.shutdown();
        m_pauseScreen.shutdown();
        m_ctx.context.popContext();
        if (m_ctx.context.getCurrentContext() == InputContextType::Gameplay) {
            m_ctx.input.captureMouse(true);
        }
    }

    void update(float dt, const InputSnapshot& snapshot) override {
        const UIInputRouteResult uiRouteResult =
            UIInputAdapter::routeInput(m_ctx.uiRenderer, snapshot, m_ctx.context);

        const bool cancel = m_ctx.context.isActionTriggered(Action::Cancel);
        const bool menu = m_ctx.context.isActionTriggered(Action::Menu);

        // Update the currently visible screen's animations
        if (m_showingSettings) {
            m_settingsScreen.updateAnimations(dt);
        } else {
            m_pauseScreen.updateAnimations(dt);
        }

        if ((menu || cancel) && uiRouteResult.aggregate != UIEventResult::Consumed) {
            if (m_showingSettings) {
                // Escape from settings → back to pause menu
                switchToPause();
            } else {
                // Escape from pause → resume gameplay
                m_ctx.fsm.popState();
            }
            return;
        }

        if (m_quitToMenu) {
            m_quitToMenu = false;
            m_ctx.fsm.requestQuitToMenu();
            m_ctx.fsm.popState();
            return;
        }
    }

private:
    void switchToSettings() {
        m_showingSettings = true;
        m_pauseScreen.exitScene();
        m_ctx.uiRenderer.setActiveScene(&m_settingsScreen);
        m_settingsScreen.enterScene();
    }

    void switchToPause() {
        m_showingSettings = false;
        m_settingsScreen.exitScene();
        m_ctx.uiRenderer.setActiveScene(&m_pauseScreen);
        m_pauseScreen.enterScene();
    }

    UIStateContext m_ctx;
    PauseMenuScreen m_pauseScreen;
    SettingsScreen m_settingsScreen;
    bool m_showingSettings = false;
    bool m_quitToMenu = false;
};

#endif //MECRAFT_UISTATE_H
