#include "GameplayAppState.h"
#include "MainMenuAppState.h"
#include "game/Game.h"

GameplayAppState::GameplayAppState(AppStateDependencies deps) 
    : m_deps(deps) {
}

GameplayAppState::~GameplayAppState() {
    if (m_game) {
        m_game->shutdown();
    }
}

void GameplayAppState::onEnter() {
    GameSessionConfig config{1234, 16};
    GameSessionDependencies deps{
        m_deps.window,
        m_deps.input,
        m_deps.actionMap,
        m_deps.contextManager,
        m_deps.resourceMgr,
        m_deps.audioEngine,
        m_deps.bgmSystem,
        m_deps.uiRenderer,
        m_deps.localeManager
    };

    m_game = std::make_unique<Game>(config, deps);
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
        m_game->fixedUpdate(kFixedStep, accumulator);
        if (m_game->isQuitToMenuRequested()) {
            m_game->clearQuitToMenuRequest();
            m_deps.appFsm.changeState(std::make_unique<MainMenuAppState>(m_deps));
            accumulator = 0.0;
            return;
        }
    }
    m_game->updateFrame(static_cast<float>(frameTime));

    // Check if the pause menu requested quit-to-menu
    if (m_game->isQuitToMenuRequested()) {
        m_game->clearQuitToMenuRequest();
        m_deps.appFsm.changeState(std::make_unique<MainMenuAppState>(m_deps));
        accumulator = 0.0;
    }
}

void GameplayAppState::render(double frameTime) {
    if (m_game) {
        m_game->renderFrame(static_cast<float>(frameTime));
    }
}
