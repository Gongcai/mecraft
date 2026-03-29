//
// Created by Caiwe on 2026/3/21.
//
#include "Game.h"
#include "states/GameplayState.h"
#include "../world/Block.h"
#include "../world/LightEngine.h"

Game::Game() : m_contextManager(m_actionMap,m_input), m_physicsSystem(&m_world), m_lightEngine(std::make_unique<LightEngine>(&m_world)) {
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
    m_resourceMgr.buildTextureAtlas("../assets/textures/blocks", 16);
    m_resourceMgr.generateLightmap();  // Generate lightmap texture
    BlockRegistry::init(&m_resourceMgr);
    BlockRegistry::printAllBlocks();
    m_world.init(1337);
    m_world.setRenderDistance(8);
    m_world.setLightEngine(m_lightEngine.get());  // Set light engine for world
    // 初始化玩家
    m_player.init({0.0f, static_cast<float>(m_world.getFlatSurfaceY() + 2), 0.0f});
    // 初始化渲染器
    m_renderer.init(m_resourceMgr);
    glEnable(GL_DEPTH_TEST);

    // Push initial Gameplay state
    m_stateMachine.pushState(std::make_unique<GameplayState>(
        m_stateMachine,
        m_player,
        m_contextManager,
        m_input,
        m_physicsSystem,
        m_world
    ));

    // 初始化信息仪表盘
    m_dashboard.init(m_window);
}

void Game::run() {
    constexpr double kFixedStep = 1.0 / 60.0;
    constexpr double kMaxFrameTime = 0.25; // Avoid huge catch-up loops after stalls.
    double accumulator = 0.0;

    while (!m_window.shouldClose()) {
        m_window.pollEvents();
        Time::update();

        double frameTime = Time::deltaTime;
        if (frameTime > kMaxFrameTime) {
            frameTime = kMaxFrameTime;
        }
        accumulator += frameTime;

        while (accumulator >= kFixedStep) {
            m_input.update();
            m_stateMachine.update(static_cast<float>(kFixedStep), m_input.snapshot());
            accumulator -= kFixedStep;
            m_world.update(m_player.getPosition());
        }

        m_renderer.render(m_world, m_player.getCamera(), m_window, m_player);
        m_dashboard.render(m_player, m_world,m_player.getCamera(),m_renderer);
        m_window.swapBuffers();

    }
}

void Game::shutdown() {
    // IMPORTANT: Shutdown order matters!
    // 1. First stop the meshing service to prevent accessing chunks being destroyed
    m_renderer.shutdown();
    // 2. Then destroy all chunks (with OpenGL context still valid)
    m_world.shutdown();
    // 3. Finally destroy window and OpenGL context
    m_window.destroy();
    m_shutdownCalled = true;
}

Game::~Game() {
    if (!m_shutdownCalled) {
        shutdown();
    }
}
