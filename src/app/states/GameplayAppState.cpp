#include "GameplayAppState.h"
#include "MainMenuAppState.h"
#include "../../Diagnostics.h"
#include "game/Game.h"
#include "app/validation/ValidationRunController.h"
#include "engine/platform/Time.h"
#include "renderer/capture/TextureCapture.h"
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

bool GameplayAppState::beginValidation() {
    if (!m_deps.validationRun.enabled()) {
        return true;
    }
    const app::validation::ValidationSceneContract& contract =
        m_deps.validationRun.sceneContract();
    if (!m_game->prepareValidationScene(
            static_cast<float>(contract.environment.timeOfDaySeconds))) {
        m_deps.validationRun.fail(
            app::validation::ValidationRunError::SceneInitializationFailed,
            "gameplay validation requires a loaded local session with persistence disabled and fixed renderer settings");
        m_enterFailed = true;
        return false;
    }
    if (!m_deps.validationRun.beginScene(ValidationScene::Voxel)) {
        m_enterFailed = true;
        return false;
    }
    m_previousTimeSpeed = Time::getTimeSpeed();
    Time::setTimeSpeed(0.0);
    m_validationActive = true;
    m_validationSceneReady = false;
    m_deps.input.captureMouse(false);
    return true;
}

void GameplayAppState::onEnter() {
    if (m_game && m_game->isInitialized()) {
        m_deps.input.captureMouse(true);
        if (m_deps.beginGameplayInputReplay) {
            m_deps.beginGameplayInputReplay();
        }
        static_cast<void>(beginValidation());
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
    static_cast<void>(beginValidation());
}

void GameplayAppState::onExit() {
    if (m_deps.endGameplayInputReplay) {
        m_deps.endGameplayInputReplay();
    }
    if (m_game) {
        m_game->shutdown();
        m_game.reset();
    }
    if (m_validationActive) {
        Time::setTimeSpeed(m_previousTimeSpeed);
        m_validationActive = false;
    }
    m_validationSceneReady = false;
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

    if (m_validationActive) {
        accumulator = 0.0;
        if (!m_game->updateFrame(0.0f)) {
            m_deps.validationRun.fail(
                app::validation::ValidationRunError::RenderFailed,
                "gameplay validation frame update failed");
        }
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
        if (m_validationActive) {
            const Window::FramebufferSize framebufferSize =
                m_deps.window.getFramebufferSize();
            const AppLaunchOptions& options =
                m_deps.validationRun.options();
            if (framebufferSize.width !=
                    static_cast<int>(options.validationWidth) ||
                framebufferSize.height !=
                    static_cast<int>(options.validationHeight)) {
                m_deps.validationRun.fail(
                    app::validation::ValidationRunError::RenderFailed,
                    "gameplay validation framebuffer extent does not match the requested capture extent");
                return;
            }
            const app::validation::ValidationFrame* validationFrame =
                m_deps.validationRun.currentFrame();
            if (validationFrame == nullptr) {
                m_deps.validationRun.fail(
                    app::validation::ValidationRunError::InvalidState,
                    "gameplay validation has no active frame");
                return;
            }
            const RenderFrameClock clock{
                validationFrame->sequenceFrameIndex,
                validationFrame->deltaTimeSeconds,
                validationFrame->renderTimeSeconds,
                validationFrame->renderTimeSeconds};
            const std::filesystem::path* capturePath =
                m_validationSceneReady && validationFrame->captureAfterRender
                ? &options.validationCapturePath
                : nullptr;
            if (!m_game->configureValidationFrame(
                    validationFrame->cameraPose, clock, capturePath)) {
                m_deps.validationRun.fail(
                    app::validation::ValidationRunError::CameraPoseConversionFailed,
                    "gameplay Camera Path pose cannot be represented by the float render camera");
                return;
            }
            if (!m_game->renderFrame(validationFrame->deltaTimeSeconds)) {
                m_deps.validationRun.fail(
                    app::validation::ValidationRunError::RenderFailed,
                    "gameplay validation scene rendering failed");
                return;
            }

            if (!m_validationSceneReady) {
                m_validationSceneReady =
                    m_game->isValidationSceneReady();
                return;
            }

            bool captureSucceeded = true;
            std::string captureDetail;
            if (validationFrame->captureAfterRender) {
                std::optional<renderer::capture::TextureCaptureResult> result =
                    m_game->takeValidationCaptureResult();
                captureSucceeded = result.has_value() && result->succeeded();
                if (!captureSucceeded) {
                    captureDetail = result.has_value()
                        ? std::string(renderer::capture::textureCaptureErrorStableId(
                              result->error)) + ":" + result->detail
                        : "capture callback did not publish a result";
                }
            }
            static_cast<void>(m_deps.validationRun.completeFrame(
                captureSucceeded, std::move(captureDetail)));
            return;
        }
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
