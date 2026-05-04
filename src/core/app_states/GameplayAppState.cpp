#include "GameplayAppState.h"

GameplayAppState::GameplayAppState(AppStateDependencies deps) 
    : m_deps(deps) {
}

GameplayAppState::~GameplayAppState() {
    if (m_game) {
        m_game->shutdown();
    }
}

void GameplayAppState::onEnter() {
    GameInitParams params;
    params.window = &m_deps.window;
    params.input = &m_deps.input;
    params.actionMap = &m_deps.actionMap;
    params.contextManager = &m_deps.contextManager;
    params.resourceMgr = &m_deps.resourceMgr;
    params.audioEngine = &m_deps.audioEngine;
    params.bgmSystem = &m_deps.bgmSystem;
    params.uiRenderer = &m_deps.uiRenderer;
    params.seed = 1234;

    m_game = std::make_unique<Game>(params);
    m_game->init();

    m_deps.input.captureMouse(true);
}

void GameplayAppState::onExit() {
    if (m_game) {
        m_game->shutdown();
        m_game.reset();
    }
    m_deps.input.captureMouse(false);
}

void GameplayAppState::update(double frameTime, double& accumulator) {
    constexpr double kFixedStep = 1.0 / 60.0;
    while (accumulator >= kFixedStep) {
        m_game->runFixedUpdate(kFixedStep, accumulator);
    }
    m_game->syncAudioListener(static_cast<float>(frameTime));
}

void GameplayAppState::render(double frameTime) {
    m_game->renderFrame(static_cast<float>(frameTime));
}
