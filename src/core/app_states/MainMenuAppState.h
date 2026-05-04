#ifndef MECRAFT_MAINMENUAPPSTATE_H
#define MECRAFT_MAINMENUAPPSTATE_H

#include "IAppState.h"
#include "AppStateMachine.h"
#include "AppStateDependencies.h"
#include "GameplayAppState.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>

class MainMenuAppState : public IAppState {
public:
    explicit MainMenuAppState(AppStateDependencies deps)
        : m_deps(deps) {}

    void onEnter() override {
        m_deps.contextManager.pushContext(InputContextType::UI);
        m_deps.input.captureMouse(false);
    }

    void onExit() override {
        m_deps.contextManager.popContext();
    }

    void update(double frameTime, double& accumulator) override {
        (void)frameTime;
        m_deps.input.update();
        if (m_deps.input.snapshot().isKeyJustPressed(GLFW_KEY_SPACE)) {
            m_deps.appFsm.changeState(std::make_unique<GameplayAppState>(m_deps));
            accumulator = 0.0;
            return;
        }
        accumulator = 0.0; // Consume accumulator so physics doesn't spiral
    }

    void render(double frameTime) override {
        (void)frameTime;
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        float screenWidth = static_cast<float>(m_deps.window.getWidth());
        float screenHeight = static_cast<float>(m_deps.window.getHeight());
        float scale = 4.0f;
        std::array<float, 4> color = {1.0f, 1.0f, 1.0f, 1.0f};

        float textWidth = 10.0f * 8.0f * scale;
        float textHeight = 8.0f * scale;
        float x = (screenWidth - textWidth) / 2.0f;
        float y = (screenHeight - textHeight) / 2.0f;

        m_deps.uiRenderer.renderText("start game", x, y, scale, color, screenWidth, screenHeight);
        m_deps.window.swapBuffers();
    }

private:
    AppStateDependencies m_deps;
};

#endif // MECRAFT_MAINMENUAPPSTATE_H
