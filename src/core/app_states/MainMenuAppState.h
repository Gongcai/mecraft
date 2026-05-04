#ifndef MECRAFT_MAINMENUAPPSTATE_H
#define MECRAFT_MAINMENUAPPSTATE_H

#include "IAppState.h"
#include "AppStateMachine.h"
#include "AppStateDependencies.h"
#include "GameplayAppState.h"
#include "../../ui/screens/MainMenuScreen.h"
#include "../../ui/ScreenTransition.h"
#include "../../ui/UIInputEvent.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>

class MainMenuAppState : public IAppState {
public:
    explicit MainMenuAppState(AppStateDependencies deps)
        : m_deps(deps) {}

    void onEnter() override {
        m_deps.contextManager.pushContext(InputContextType::UI);
        m_deps.input.captureMouse(false);

        m_screen.init(m_deps.resourceMgr);
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
        m_screen.shutdown();
        m_transition.shutdown();
        m_deps.contextManager.popContext();
    }

    void update(double frameTime, double& accumulator) override {
        m_deps.input.update();
        const auto& snapshot = m_deps.input.snapshot();

        // Route UI input to the screen
        const float mx = snapshot.mousePosition.x;
        const float my = snapshot.mousePosition.y;

        static_cast<void>(m_deps.uiRenderer.routeUIInput({UIInputEventType::PointerMove, mx, my, UIPointerButton::None}));

        if (snapshot.mouseButtonsJustPressed[GLFW_MOUSE_BUTTON_LEFT]) {
            static_cast<void>(m_deps.uiRenderer.routeUIInput({UIInputEventType::PointerDown, mx, my, UIPointerButton::Primary}));
        }
        if (snapshot.mouseButtonsJustReleased[GLFW_MOUSE_BUTTON_LEFT]) {
            static_cast<void>(m_deps.uiRenderer.routeUIInput({UIInputEventType::PointerUp, mx, my, UIPointerButton::Primary}));
        }

        m_screen.updateAnimations(static_cast<float>(frameTime));

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
        glClearColor(0.05f, 0.05f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

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
};

#endif // MECRAFT_MAINMENUAPPSTATE_H
