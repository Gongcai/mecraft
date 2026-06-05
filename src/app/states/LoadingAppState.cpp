#include "LoadingAppState.h"

#include "GameplayAppState.h"
#include "MainMenuAppState.h"
#include "../../game/Game.h"

#include <cstdio>
#include <exception>
#include <iostream>

LoadingAppState::LoadingAppState(AppStateDependencies deps, GameSessionConfig config)
    : m_deps(deps), m_config(std::move(config)) {
}

LoadingAppState::~LoadingAppState() {
    if (m_game) {
        m_game->shutdown();
    }
}

void LoadingAppState::onEnter() {
    m_deps.contextManager.pushContext(InputContextType::UI);
    m_deps.input.captureMouse(false);

    m_screen.setLocaleManager(&m_deps.localeManager);
    m_screen.init(m_deps.resourceMgr);
    m_screen.enterScene();
    m_deps.uiRenderer.setActiveScene(&m_screen);
    refreshScreen();
}

void LoadingAppState::onExit() {
    m_deps.uiRenderer.setActiveScene(nullptr);
    m_screen.exitScene();
    m_screen.shutdown();
    m_deps.contextManager.popContext();
}

void LoadingAppState::update(const double frameTime, double& accumulator) {
    m_deps.input.update();
    m_screen.updateAnimations(static_cast<float>(frameTime));

    if (!m_firstFrameRendered) {
        accumulator = 0.0;
        return;
    }

    try {
        if (!m_game) {
            m_game = createGame();
            m_game->beginLoading();
        }

        if (!m_game->isLoadingComplete()) {
            m_game->updateLoading(static_cast<float>(frameTime));
        }

        refreshScreen();

        if (m_game->isLoadingComplete()) {
            m_deps.appFsm.changeState(
                std::make_unique<GameplayAppState>(m_deps, std::move(m_game)));
            accumulator = 0.0;
            return;
        }
    } catch (const std::exception& ex) {
        std::cerr << "[LoadingAppState] Failed to load gameplay: " << ex.what() << '\n';
        if (m_game) {
            m_game->shutdown();
            m_game.reset();
        }
        m_failed = true;
        m_deps.appFsm.changeState(std::make_unique<MainMenuAppState>(m_deps));
    }

    accumulator = 0.0;
}

void LoadingAppState::render(const double frameTime) {
    (void)frameTime;
    glClearColor(0.03f, 0.04f, 0.05f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    m_deps.uiRenderer.renderSceneOnly(m_deps.window, m_deps.input.snapshot());
    m_deps.window.swapBuffers();
    m_firstFrameRendered = true;
}

std::unique_ptr<Game> LoadingAppState::createGame() const {
    GameSessionDependencies deps{
        m_deps.window,
        m_deps.input,
        m_deps.actionMap,
        m_deps.contextManager,
        m_deps.resourceMgr,
        m_deps.audioEngine,
        m_deps.bgmSystem,
        m_deps.uiRenderer,
        m_deps.localeManager,
        m_deps.threadPool
    };
    return std::make_unique<Game>(m_config, deps);
}

void LoadingAppState::refreshScreen() {
    if (!m_game) {
        m_screen.setProgress(0.0f);
        m_screen.setStatusText("Preparing world");
        m_screen.setDetailText("");
        return;
    }

    const Game::LoadProgress progress = m_game->getLoadProgress();
    m_screen.setProgress(progress.progress);
    m_screen.setStatusText(progress.label);

    if (progress.targetChunks > 0) {
        char detail[96];
        std::snprintf(detail, sizeof(detail), "%d / %d chunks ready",
                      progress.loadedChunks,
                      progress.targetChunks);
        m_screen.setDetailText(detail);
    } else if (m_failed) {
        m_screen.setDetailText("Returning to menu");
    } else {
        m_screen.setDetailText("");
    }
}
