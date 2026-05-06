#ifndef MECRAFT_MAINMENUAPPSTATE_H
#define MECRAFT_MAINMENUAPPSTATE_H

#include "IAppState.h"
#include "AppStateMachine.h"
#include "AppStateDependencies.h"
#include "GameplayAppState.h"
#include "../../ui/screens/MainMenuScreen.h"
#include "../../ui/ScreenTransition.h"
#include "../../ui/UIInputAdapter.h"
#include "../../renderer/SkyboxRenderer.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>

class MainMenuAppState : public IAppState {
public:
    explicit MainMenuAppState(AppStateDependencies deps)
        : m_deps(deps) {}

    void onEnter() override {
        m_deps.contextManager.pushContext(InputContextType::UI);
        m_deps.input.captureMouse(false);

        m_screen.setLocaleManager(&m_deps.localeManager);
        m_screen.init(m_deps.resourceMgr);
        m_skyboxRenderer.init(m_deps.resourceMgr);
        m_skyboxYaw = 0.0f;
        m_screen.onStartClicked = [this]() {
            m_transitioningToGame = true;
            m_transition.startFadeOut(0.5f);
        };
        m_screen.onQuitClicked = [this]() {
            glfwSetWindowShouldClose(m_deps.window.getHandle(), true);
        };
        m_deps.uiRenderer.setActiveScene(&m_screen);
        m_screen.enterScene();

        m_transition.init(m_deps.resourceMgr);
        m_transitioningToGame = false;
    }

    void onExit() override {
        m_deps.uiRenderer.setActiveScene(nullptr);
        m_skyboxRenderer.shutdown();
        m_screen.shutdown();
        m_transition.shutdown();
        m_deps.contextManager.popContext();
    }

    void update(double frameTime, double& accumulator) override {
        m_deps.input.update();
        const auto& snapshot = m_deps.input.snapshot();

        const UIInputRouteResult uiRouteResult =
            UIInputAdapter::routeInput(m_deps.uiRenderer, snapshot, m_deps.contextManager);

        const bool cancel = m_deps.contextManager.isActionTriggered(Action::Cancel);
        const bool menu = m_deps.contextManager.isActionTriggered(Action::Menu);

        if ((menu || cancel) && uiRouteResult.aggregate != UIEventResult::Consumed) {
            glfwSetWindowShouldClose(m_deps.window.getHandle(), true);
            accumulator = 0.0;
            return;
        }

        m_screen.updateAnimations(static_cast<float>(frameTime));
        m_skyboxYaw += static_cast<float>(frameTime) * 3.0f;

        if (m_transitioningToGame) {
            m_transition.tick(static_cast<float>(frameTime));
            if (m_transition.isDone()) {
                m_deps.appFsm.changeState(std::make_unique<GameplayAppState>(m_deps));
                accumulator = 0.0;
                return;
            }
        }
        accumulator = 0.0;
    }

    void render(double frameTime) override {
        (void)frameTime;
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        m_skyboxRenderer.render(m_deps.window.getAspectRatio(), m_skyboxYaw, 10.0f);

        m_deps.uiRenderer.renderSceneOnly(m_deps.window, m_deps.input.snapshot());

        if (m_transitioningToGame) {
            m_transition.render(m_deps.window.getWidth(), m_deps.window.getHeight());
        }

        m_deps.window.swapBuffers();
    }

private:
    AppStateDependencies m_deps;
    MainMenuScreen m_screen;
    ScreenTransition m_transition;
    bool m_transitioningToGame = false;
    SkyboxRenderer m_skyboxRenderer;
    float m_skyboxYaw = 0.0f;
};

#endif // MECRAFT_MAINMENUAPPSTATE_H
