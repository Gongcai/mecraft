//
// Created by Caiwe on 2026/3/21.
//
#include "Game.h"
#include "states/GameStateMachine.h"
#include "render/GameplayRenderRuntime.h"
#include "audio/AudioListenerSyncSystem.h"
#include "orchestrator/GameFrameOrchestrator.h"
#include "presentation/GameplayHudPresenter.h"

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

void Game::init() {
    if (m_initialized) {
        return;
    }
    m_initialized = true;
    m_session.init(m_config, m_deps.resourceMgr, m_renderRuntime->getThreadPool());
    m_session.initWorld(m_config.seed);
    m_renderRuntime->init(m_deps.resourceMgr, m_session, m_deps.uiRenderer);
    m_session.initECS(m_deps);
    m_session.initStateMachine(m_deps);

    // G3: Initialize audio sync and HUD presenter
    m_audioSyncSystem = std::make_unique<AudioListenerSyncSystem>(m_deps.bgmSystem, m_deps.audioEngine);
    m_hudPresenter = std::make_unique<GameplayHudPresenter>(m_deps.window, m_deps.uiRenderer, m_deps.input);

#ifdef MECRAFT_DEBUG
    m_renderRuntime->initDebug(m_deps.window);
    // Inject Dashboard into presenter (renderRuntime owns, presenter renders)
    m_hudPresenter->setDashboard(m_renderRuntime->dashboard());
#endif
}

void Game::fixedUpdate(const double fixedStep, double& accumulator) {
    m_frameOrchestrator->runFixedUpdate(m_session, m_deps.input, m_renderRuntime.get(), fixedStep, accumulator);
}

void Game::updateFrame(const float deltaTime) {
    // G5: Delegate to orchestrator
#ifdef MECRAFT_DEBUG
    const auto audioStart = std::chrono::steady_clock::now();
#endif
    if (m_audioSyncSystem) {
        m_frameOrchestrator->syncAudioListener(*m_audioSyncSystem, deltaTime, m_session);
    } else {
        // Fallback during early init before audioSyncSystem is created
        AudioListenerSyncSystem fallback(m_deps.bgmSystem, m_deps.audioEngine);
        m_frameOrchestrator->syncAudioListener(fallback, deltaTime, m_session);
    }
#ifdef MECRAFT_DEBUG
    const auto audioEnd = std::chrono::steady_clock::now();
    if (m_renderRuntime) {
        if (auto* profiler = m_renderRuntime->profiler()) {
            profiler->recordAudio(std::chrono::duration<double, std::milli>(audioEnd - audioStart).count());
        }
    }
#endif
}

void Game::renderFrame(const float frameTime) {
    if (!m_initialized) {
        return;
    }

#ifdef MECRAFT_DEBUG
    m_renderRuntime->publishDebugStats(frameTime);
#endif

    // G5: Delegate frame rendering to orchestrator
    m_frameOrchestrator->renderFrame(m_session, *m_renderRuntime,
                                     m_hudPresenter.get(),
                                     m_deps.window, frameTime);
}

void Game::shutdown() {
    if (!m_initialized) {
        return;
    }
    m_renderRuntime->shutdown();
    m_session.shutdown();
    m_initialized = false;
}

bool Game::isQuitToMenuRequested() const {
    return m_session.stateMachine().isQuitToMenuRequested();
}

void Game::clearQuitToMenuRequest() {
    m_session.stateMachine().clearQuitToMenuRequest();
}
