#ifndef MECRAFT_UISTATE_H
#define MECRAFT_UISTATE_H

#include "IGameState.h"
#include "UIStateContext.h"
#include "../../ui/core/UIInputAdapter.h"
#include "../../ui/screens/PauseMenuScreen.h"
#include "engine/input/InputContextManager.h"
#include "ui/core/UIRenderer.h"

/// G6: UIState uses UIStateContext — only UI-relevant dependencies.
/// Does not create child states, so narrow context is safe.
class UIState : public IGameState {
public:
    explicit UIState(UIStateContext ctx)
            : m_ctx(ctx) {}

    void onEnter() override {
        m_ctx.context.pushContext(InputContextType::UI);
        m_ctx.input.captureMouse(false);

        ResourceMgr* rm = m_ctx.uiRenderer.getResourceMgr();
        if (rm) {
            m_pauseScreen.setLocaleManager(&m_ctx.localeManager);
            m_pauseScreen.init(*rm);
            m_pauseScreen.onResume = [this]() {
                m_ctx.fsm.popState();
            };
            m_pauseScreen.onQuitToMenu = [this]() {
                m_quitToMenu = true;
            };
            m_ctx.uiRenderer.setActiveScene(&m_pauseScreen);
            m_pauseScreen.enterScene();
        }
    }

    void onExit() override {
        m_ctx.uiRenderer.setActiveScene(nullptr);
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

        m_pauseScreen.updateAnimations(dt);

        if ((menu || cancel) && uiRouteResult.aggregate != UIEventResult::Consumed) {
            m_ctx.fsm.popState();
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
    UIStateContext m_ctx;
    PauseMenuScreen m_pauseScreen;
    bool m_quitToMenu = false;
};

#endif //MECRAFT_UISTATE_H
