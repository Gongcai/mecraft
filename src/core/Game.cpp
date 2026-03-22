//
// Created by Caiwe on 2026/3/21.
//
#include "Game.h"


void Game::init(int width, int height, const char *title) {
    // 初始化opengl环境
    if (m_window.init(width, height, title)) {
        m_input.init(m_window.getHandle());
        m_input.captureMouse(true);
    }
    else {
        std::cerr << "Error while initializing the window." << std::endl;
    }
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
        m_player.update(static_cast<float>(Time::deltaTime), m_input.snapshot());
        m_renderer.render(m_player.getCamera(), m_window);
    }
}

void Game::shutdown() {
    return;
}

