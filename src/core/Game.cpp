//
// Created by Caiwe on 2026/3/21.
//
#include "Game.h"


Game::Game() : m_contextManager(m_actionMap) {
}

void Game::init(int width, int height, const char *title) {
    // 初始化opengl环境
    if (m_window.init(width, height, title)) {
        m_input.init(m_window.getHandle());
        m_input.captureMouse(true);
    }
    else {
        std::cerr << "Error while initializing the window." << std::endl;
    }
    // Load bindings
    m_actionMap.loadFromFile("../assets/config/keybindings.txt");


    // 初始化时间
    Time::init();
    // 初始化资源管理器，加载着色器/贴图
    m_resourceMgr.init();
    // 初始化玩家
    m_player.init({0.0f, 1.0f, 0.0f});
    // 初始化渲染器
    m_renderer.init(m_resourceMgr);
    glEnable(GL_DEPTH_TEST);
}

void Game::run() {

    while (!m_window.shouldClose()) {
        m_window.pollEvents();
        Time::update();
        m_input.update();

        handleGlobalInput();

        // Only update player if Gameplay is the active context (Pause/UI blocks gameplay input)
        if (m_contextManager.getCurrentContext() == InputContextType::Gameplay) {
            m_player.update(static_cast<float>(Time::deltaTime), m_input.snapshot(), m_contextManager);
        }

        m_renderer.render(m_player.getCamera(), m_window);
    }
}

void Game::shutdown() {
    return;
}

void Game::handleGlobalInput() {
    const auto& snapshot = m_input.snapshot();
    if (m_contextManager.isActionTriggered(Action::Menu, snapshot)) {
        if (m_contextManager.getCurrentContext() == InputContextType::Gameplay) {
            m_contextManager.pushContext(InputContextType::UI);
            m_input.captureMouse(false);
        } else {
            m_contextManager.popContext();
            if (m_contextManager.getCurrentContext() == InputContextType::Gameplay) {
                m_input.captureMouse(true);
            }
        }
    }
}
