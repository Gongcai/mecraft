//
// Created by Caiwe on 2026/3/21.
//
#include "Game.h"
#include "../Diagnostics.h"
#include "states/GameStateMachine.h"
#include "render/GameplayRenderRuntime.h"
#include "audio/AudioListenerSyncSystem.h"
#include "orchestrator/GameFrameOrchestrator.h"
#include "presentation/GameplayHudPresenter.h"
#include "server/GameServer.h"
#include "save/SaveManager.h"
#include <glad/glad.h>
#include <iostream>

#ifdef MECRAFT_DEBUG
#include "debug/DebugFrameProfiler.h"
#include <chrono>
#endif

// DebugRuntime struct has been migrated to GameplayRenderRuntime

Game::Game(GameSessionConfig config, GameSessionDependencies deps)
    : m_config(std::move(config)),
      m_deps(std::move(deps)),
      m_renderRuntime(std::make_unique<GameplayRenderRuntime>()),
      m_hudPresenter(nullptr),
      m_audioSyncSystem(nullptr),
      m_frameOrchestrator(std::make_unique<GameFrameOrchestrator>()) {
}

Game::~Game() = default;

bool Game::init() {
    beginLoading();
    while (!isLoadingComplete()) {
        if (!updateLoading(1.0f / 60.0f)) {
            return false;
        }
    }
    return true;
}

void Game::beginLoading() {
    if (m_loadPhase != LoadPhase::NotStarted) {
        return;
    }
    m_initialized = true;
    m_loadPhase = LoadPhase::Session;
}

bool Game::updateLoading(const float deltaTime) {
    if (m_loadPhase == LoadPhase::NotStarted) {
        beginLoading();
    }

    switch (m_loadPhase) {
    case LoadPhase::Session:
        m_session.init(m_config, m_deps.resourceMgr, &m_deps.threadPool);
        m_session.initWorld(m_config.seed);
        m_loadPhase = LoadPhase::RenderRuntime;
        break;
    case LoadPhase::RenderRuntime:
        if (!m_renderRuntime->init(m_deps.resourceMgr,
                                   m_session,
                                   m_deps.uiRenderer,
                                   m_deps.threadPool,
                                   m_deps.window)) {
            MECRAFT_LOG_STREAM(std::cerr << "[Game] Render runtime initialization failed\n");
            m_loadPhase = LoadPhase::Failed;
            return false;
        }
        m_session.setRenderScene(&m_renderRuntime->renderScene());
        m_loadPhase = LoadPhase::Ecs;
        break;
    case LoadPhase::Ecs:
        m_session.initECS(m_deps);
        m_session.loadLocalPlayer();  // Restore saved player state before preloading chunks.
        m_session.initStateMachine(m_deps);

        m_audioSyncSystem = std::make_unique<AudioListenerSyncSystem>(m_deps.bgmSystem, m_deps.audioEngine);
        m_hudPresenter = std::make_unique<GameplayHudPresenter>(m_deps.window, m_deps.uiRenderer, m_deps.input);

#ifdef MECRAFT_DEBUG
        if (m_deps.enableDebugDashboard) {
            m_renderRuntime->initDebug(m_deps.window);
            m_hudPresenter->setDashboard(m_renderRuntime->dashboard());
        }
#endif
        m_loadPhase = LoadPhase::InitialChunks;
        break;
    case LoadPhase::InitialChunks: {
        constexpr int kMaxPumpsPerFrame = 4;
        constexpr float kInitialLoadTickDt = 1.0f / 20.0f;
        const int pumpCount = deltaTime > 0.03f ? 1 : kMaxPumpsPerFrame;
        for (int i = 0; i < pumpCount && !m_session.isInitialChunkLoadComplete(); ++i) {
            m_session.pumpInitialChunkLoad(kInitialLoadTickDt);
        }
        if (m_session.isInitialChunkLoadComplete() &&
            m_session.stabilizeLocalPlayerAfterInitialLoad()) {
            m_loadPhase = LoadPhase::Complete;
        }
        break;
    }
    case LoadPhase::Complete:
    case LoadPhase::Failed:
    case LoadPhase::NotStarted:
        break;
    }
    return m_loadPhase != LoadPhase::Failed;
}

bool Game::fixedUpdate(const double fixedStep, double& accumulator) {
    m_frameOrchestrator->runFixedUpdate(m_session, m_deps.input, m_renderRuntime.get(), fixedStep, accumulator);
    return true;
}

bool Game::updateFrame(const float deltaTime) {
    // G5: Delegate to orchestrator
#ifdef MECRAFT_DEBUG
    const auto audioStart = std::chrono::steady_clock::now();
#endif
    if (m_audioSyncSystem) {
        m_frameOrchestrator->syncAudioListener(*m_audioSyncSystem, deltaTime, m_session);
    } else {
        MECRAFT_LOG_STREAM(std::cerr << "[Game] Audio listener system is not initialized\n");
        return false;
    }
#ifdef MECRAFT_DEBUG
    const auto audioEnd = std::chrono::steady_clock::now();
    if (m_renderRuntime) {
        if (auto* profiler = m_renderRuntime->profiler()) {
            profiler->recordAudio(std::chrono::duration<double, std::milli>(audioEnd - audioStart).count());
        }
    }
#endif
    return true;
}

void Game::setFixedInterpolationAlpha(const float alpha) {
    m_fixedInterpolationAlpha = std::clamp(alpha, 0.0f, 1.0f);
}

#ifdef MECRAFT_DEBUG
void Game::publishDebugStats(const float frameTime) {
    if (m_renderRuntime) {
        m_renderRuntime->publishDebugStats(frameTime);
    }
}

void Game::recordPollEvents(double ms,
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
    if (m_renderRuntime) {
        if (auto* profiler = m_renderRuntime->profiler()) {
            profiler->recordPollEvents(ms, keyEvents, mouseButtonEvents, cursorPosEvents, scrollEvents, charEvents,
                                       inputCallbackMs, cursorPosCallbackMs, imguiCallbackMs,
                                       imguiCursorPosCallbackMs, imguiCursorPosBackendMs, imguiWndProcMs,
                                       imguiWndProcSlowestMs, imguiWndProcSlowestMsg, imguiWndProcCount);
        }
    }
}

void Game::recordAppUpdateDispatch(double ms) {
    if (m_renderRuntime) {
        if (auto* profiler = m_renderRuntime->profiler()) {
            profiler->recordAppUpdateDispatch(ms);
        }
    }
}

void Game::recordAppRenderDispatch(double ms) {
    if (m_renderRuntime) {
        if (auto* profiler = m_renderRuntime->profiler()) {
            profiler->recordAppRenderDispatch(ms);
        }
    }
}
#endif

bool Game::renderFrame(const float frameTime) {
    if (!m_initialized) {
        return true;
    }

    // Set screenshot capture callback if requested (captures before UI overlay)
    if (m_captureScreenshotOnNextFrame) {
        m_captureScreenshotOnNextFrame = false;
        m_frameOrchestrator->setPreUiCallback([this]() { captureExitScreenshot(); });
    }

    // G5: Delegate frame rendering to orchestrator
    const float renderInterpolationAlpha = m_session.stateMachine().pausesSimulation()
        ? 1.0f
        : m_fixedInterpolationAlpha;
    return m_frameOrchestrator->renderFrame(m_session,
                                            *m_renderRuntime,
                                            m_hudPresenter.get(),
                                            m_deps.window,
                                            frameTime,
                                            renderInterpolationAlpha);
}

void Game::shutdown() {
    if (!m_initialized) {
        return;
    }

    if (m_loadPhase == LoadPhase::Complete) {
        // Capture screenshot during next frame's render (before UI overlay)
        m_captureScreenshotOnNextFrame = true;

        // Render one more frame to capture the screenshot
        (void)renderFrame(0.0f);
    }

    // Session must shut down before the app-owned thread pool is stopped
    // because GameServer flushes pending saves through that pool.
    m_session.shutdown();
    m_renderRuntime->shutdown();
    m_initialized = false;
    m_loadPhase = LoadPhase::NotStarted;
}

bool Game::isQuitToMenuRequested() const {
    return m_session.stateMachine().isQuitToMenuRequested();
}

Game::LoadProgress Game::getLoadProgress() const {
    LoadProgress progress{};
    progress.phase = m_loadPhase;

    switch (m_loadPhase) {
    case LoadPhase::NotStarted:
        progress.progress = 0.0f;
        progress.label = "Preparing";
        break;
    case LoadPhase::Session:
        progress.progress = 0.08f;
        progress.label = "Loading world";
        break;
    case LoadPhase::RenderRuntime:
        progress.progress = 0.22f;
        progress.label = "Initializing renderer";
        break;
    case LoadPhase::Ecs:
        progress.progress = 0.38f;
        progress.label = "Restoring player";
        break;
    case LoadPhase::InitialChunks: {
        const auto chunkProgress = m_session.getInitialLoadProgress();
        progress.loadedChunks = chunkProgress.clientLoaded;
        progress.targetChunks = chunkProgress.target;
        progress.inFlightChunks = chunkProgress.inFlight;
        const float chunkRatio = chunkProgress.target > 0
            ? static_cast<float>(chunkProgress.clientLoaded) / static_cast<float>(chunkProgress.target)
            : 0.0f;
        progress.progress = 0.45f + std::clamp(chunkRatio, 0.0f, 1.0f) * 0.55f;
        progress.label = "Loading chunks";
        progress.complete = chunkProgress.complete;
        break;
    }
    case LoadPhase::Complete:
        progress.progress = 1.0f;
        progress.label = "Ready";
        progress.complete = true;
        break;
    case LoadPhase::Failed:
        progress.progress = 1.0f;
        progress.label = "Loading failed";
        break;
    }
    return progress;
}

void Game::captureExitScreenshot() {
    // Only capture in single-player mode with saving enabled
    if (m_config.isMultiplayer() || m_config.worldName.empty()) return;

    auto* server = &m_session.server();
    auto* sm = server->saveManager();
    if (!sm) return;

    // Read framebuffer dimensions from viewport
    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    const int w = viewport[2];
    const int h = viewport[3];
    if (w <= 0 || h <= 0) return;

    // Downscale to thumbnail size for reasonable file size
    constexpr int THUMB_W = 640;
    constexpr int THUMB_H = 360;
    const int readW = std::min(w, THUMB_W * 2);
    const int readH = std::min(h, THUMB_H * 2);

    // Read pixels from framebuffer (bottom-to-top, RGB)
    std::vector<uint8_t> pixels(readW * readH * 3);
    glReadPixels(0, 0, readW, readH, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

    // Flip vertically (OpenGL origin is bottom-left)
    std::vector<uint8_t> flipped(readW * readH * 3);
    for (int y = 0; y < readH; ++y) {
        std::memcpy(flipped.data() + y * readW * 3,
                    pixels.data() + (readH - 1 - y) * readW * 3,
                    readW * 3);
    }

    sm->saveScreenshot(flipped.data(), readW, readH);
    MECRAFT_LOG_PRINTF("[Save] Captured exit screenshot (%dx%d)\n", readW, readH);
}

void Game::clearQuitToMenuRequest() {
    m_session.stateMachine().clearQuitToMenuRequest();
}
