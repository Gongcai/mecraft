#include "GameplayAppState.h"
#include "MainMenuAppState.h"
#include "../../Diagnostics.h"
#include "game/Game.h"
#include <algorithm>
#include <iostream>

GameplayAppState::GameplayAppState(AppStateDependencies deps, GameSessionConfig config)
    : m_deps(deps), m_config(config) {
}

GameplayAppState::GameplayAppState(AppStateDependencies deps, std::unique_ptr<Game> game)
    : m_deps(deps), m_game(std::move(game)) {
}

GameplayAppState::~GameplayAppState() {
    if (m_game) {
        m_game->shutdown();
    }
}

void GameplayAppState::onEnter() {
    if (m_game && m_game->isInitialized()) {
        m_deps.input.captureMouse(true);
        if (m_deps.beginGameplayInputReplay) {
            m_deps.beginGameplayInputReplay();
        }
        return;
    }

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
        m_deps.threadPool,
        m_deps.rhiDevice,
        m_deps.commandListPool,
        m_deps.enableDebugDashboard
    };

    m_game = std::make_unique<Game>(m_config, deps);
    if (!m_game->init()) {
        MECRAFT_LOG_STREAM(std::cerr << "[GameplayAppState] Failed to enter gameplay\n");
        m_game.reset();
        m_enterFailed = true;
        m_deps.appFsm.changeState(std::make_unique<MainMenuAppState>(m_deps));
        return;
    }

    m_deps.input.captureMouse(true);
    if (m_deps.beginGameplayInputReplay) {
        m_deps.beginGameplayInputReplay();
    }
}

void GameplayAppState::onExit() {
    if (m_deps.endGameplayInputReplay) {
        m_deps.endGameplayInputReplay();
    }
    if (m_game) {
        m_game->shutdown();
        m_game.reset();
    }
    m_deps.input.captureMouse(false);
    m_quitToMenuPending = false;
    m_closeAppAfterExitScreenshot = false;
}

void GameplayAppState::update(double frameTime, double& accumulator) {
    if (m_enterFailed || !m_game) {
        accumulator = 0.0;
        return;
    }
    if (m_quitToMenuPending) {
        accumulator = 0.0;
        return;
    }

#ifdef MECRAFT_DEBUG
    m_game->publishDebugStats(static_cast<float>(frameTime));
#endif
    const auto handleQuitToMenu = [this, &accumulator]() {
        m_game->clearQuitToMenuRequest();
        m_game->requestExitScreenshot();
        m_quitToMenuPending = true;
        m_closeAppAfterExitScreenshot =
            m_deps.shouldCloseAppOnGameplayQuitToMenu &&
            m_deps.shouldCloseAppOnGameplayQuitToMenu();
        accumulator = 0.0;
    };

    constexpr double kFixedStep = 1.0 / 60.0;
    while (accumulator >= kFixedStep) {
        if (!m_game->fixedUpdate(kFixedStep, accumulator)) {
            MECRAFT_LOG_STREAM(std::cerr << "[GameplayAppState] Gameplay fixed update failed\n");
            m_game->shutdown();
            m_game.reset();
            m_enterFailed = true;
            m_deps.appFsm.changeState(std::make_unique<MainMenuAppState>(m_deps));
            accumulator = 0.0;
            return;
        }
        if (m_game->isQuitToMenuRequested()) {
            handleQuitToMenu();
            return;
        }
    }
    m_game->setFixedInterpolationAlpha(static_cast<float>(std::clamp(accumulator / kFixedStep, 0.0, 1.0)));
    if (!m_game->updateFrame(static_cast<float>(frameTime))) {
        MECRAFT_LOG_STREAM(std::cerr << "[GameplayAppState] Gameplay frame update failed\n");
        m_game->shutdown();
        m_game.reset();
        m_enterFailed = true;
        m_deps.appFsm.changeState(std::make_unique<MainMenuAppState>(m_deps));
        accumulator = 0.0;
        return;
    }

    // Check if the pause menu requested quit-to-menu
    if (m_game->isQuitToMenuRequested()) {
        handleQuitToMenu();
    }
}

void GameplayAppState::render(double frameTime) {
    if (m_game) {
        if (m_deps.window.shouldClose()) {
            m_game->requestExitScreenshot();
        }
        if (!m_game->renderFrame(static_cast<float>(frameTime))) {
            MECRAFT_LOG_STREAM(std::cerr << "[GameplayAppState] Gameplay render failed\n");
            m_game->shutdown();
            m_game.reset();
            m_enterFailed = true;
            m_quitToMenuPending = false;
            m_deps.appFsm.changeState(std::make_unique<MainMenuAppState>(m_deps));
            return;
        }
        if (m_quitToMenuPending) {
            const bool closeApp = m_closeAppAfterExitScreenshot;
            m_quitToMenuPending = false;
            m_closeAppAfterExitScreenshot = false;
            if (closeApp) {
                glfwSetWindowShouldClose(m_deps.window.getHandle(), true);
            } else {
                m_deps.appFsm.changeState(
                    std::make_unique<MainMenuAppState>(m_deps));
            }
        }
    }
}

const GpuFrameStats* GameplayAppState::gpuFrameStats() const {
    return m_game ? m_game->gpuFrameStats() : nullptr;
}

#ifdef MECRAFT_DEBUG
void GameplayAppState::recordPollEvents(double ms,
                                        unsigned keyEvents,
                                        unsigned mouseButtonEvents,
                                        unsigned cursorPosEvents,
                                        unsigned scrollEvents,
                                        unsigned charEvents,
                                        double inputCallbackMs,
                                        double cursorPosCallbackMs,
                                        double imguiCallbackMs,
                                        double imguiCursorPosCallbackMs,
                                        double imguiCursorPosBackendMs,
                                        double imguiWndProcMs,
                                        double imguiWndProcSlowestMs,
                                        unsigned imguiWndProcSlowestMsg,
                                        unsigned imguiWndProcCount) {
    if (m_game) {
        m_game->recordPollEvents(ms, keyEvents, mouseButtonEvents, cursorPosEvents, scrollEvents, charEvents,
                                 inputCallbackMs, cursorPosCallbackMs, imguiCallbackMs, imguiCursorPosCallbackMs,
                                 imguiCursorPosBackendMs, imguiWndProcMs, imguiWndProcSlowestMs,
                                 imguiWndProcSlowestMsg, imguiWndProcCount);
    }
}

void GameplayAppState::recordAppUpdateDispatch(double ms) {
    if (m_game) {
        m_game->recordAppUpdateDispatch(ms);
    }
}

void GameplayAppState::recordAppRenderDispatch(double ms) {
    if (m_game) {
        m_game->recordAppRenderDispatch(ms);
    }
}
#endif
