//
// Created by Caiwe on 2026/3/21.
//
#include "Game.h"
#include "../world/block/Block.h"
#include "../item/Item.h"
#include "../ecs/GameplayScene.h"
#include "../world/World.h"
#include "../physics/PhysicsSystem.h"
#include "../world/DropSystem.h"
#include "../particle/ParticleSystem.h"
#include "../particle/RainRenderer.h"
#include "../crafting/CraftingSystem.h"
#include "render/GameplayRenderRuntime.h"
#include "audio/AudioListenerSyncSystem.h"
#include "camera/CameraController.h"
#include "orchestrator/GameFrameOrchestrator.h"
#include "presentation/GameplayHudPresenter.h"
#include "presentation/GameplayPresentationBuilder.h"
#include "../ui/core/UIRenderer.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/vec3.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

#ifdef MECRAFT_DEBUG
#include <chrono>
#include "../ui/Dashboard.h"
#include "debug/DebugFrameProfiler.h"
#endif

#ifdef MECRAFT_DEBUG
struct Game::DebugRuntime {
    Dashboard dashboard;
    DebugFrameProfiler profiler;
    Dashboard::FrameProfilerStats dashboardProfilerStats;
};
#endif

Game::Game(const GameInitParams& params)
    : m_config{params.seed, 16, glm::vec3(5.0f, 0.0f, 0.0f)},
      m_deps{*params.window, *params.input, *params.actionMap, *params.contextManager,
             *params.resourceMgr, *params.audioEngine, *params.bgmSystem,
             *params.uiRenderer, *params.localeManager},
      m_params(params),
      m_window(*params.window),
      m_input(*params.input),
      m_actionMap(*params.actionMap),
      m_contextManager(*params.contextManager),
      m_resourceMgr(*params.resourceMgr),
      m_audioEngine(*params.audioEngine),
      m_bgmSystem(*params.bgmSystem),
      m_uiRenderer(*params.uiRenderer),
      m_localeManager(*params.localeManager),
      m_renderRuntime(std::make_unique<GameplayRenderRuntime>()),
      m_hudPresenter(nullptr),
      m_audioSyncSystem(nullptr),
      m_frameOrchestrator(std::make_unique<GameFrameOrchestrator>()) {
}

Game::Game(GameSessionConfig config, GameSessionDependencies deps)
    : m_config(std::move(config)),
      m_deps(std::move(deps)),
      m_params{&m_deps.window, &m_deps.input, &m_deps.actionMap, &m_deps.contextManager,
               &m_deps.resourceMgr, &m_deps.audioEngine, &m_deps.bgmSystem,
               &m_deps.uiRenderer, &m_deps.localeManager, m_config.seed},
      m_window(m_deps.window),
      m_input(m_deps.input),
      m_actionMap(m_deps.actionMap),
      m_contextManager(m_deps.contextManager),
      m_resourceMgr(m_deps.resourceMgr),
      m_audioEngine(m_deps.audioEngine),
      m_bgmSystem(m_deps.bgmSystem),
      m_uiRenderer(m_deps.uiRenderer),
      m_localeManager(m_deps.localeManager),
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
    m_session.init(m_config, m_resourceMgr, m_renderRuntime->getThreadPool());
    initWorld();
    m_renderRuntime->init(m_resourceMgr, m_session, m_uiRenderer);
    m_session.initECS(m_deps);

    // G3: Initialize audio sync and HUD presenter
    m_audioSyncSystem = std::make_unique<AudioListenerSyncSystem>(m_bgmSystem, m_audioEngine);
    m_hudPresenter = std::make_unique<GameplayHudPresenter>(m_window, m_uiRenderer, m_input);

    m_stateMachine.pushState(m_session.createInitialGameplayState(m_stateMachine, m_deps, m_lastSubmittedCommand));

#ifdef MECRAFT_DEBUG
    m_debug = std::make_unique<DebugRuntime>();
    m_debug->dashboard.init(m_window);
    m_debug->dashboard.setFirstPersonHeldItemRenderer(&m_renderRuntime->firstPersonHeldItemRenderer());
    // Inject Dashboard into presenter (Game owns, presenter renders)
    m_hudPresenter->setDashboard(&m_debug->dashboard);
#endif
}


void Game::initWorld() {
    m_session.initWorld(m_config.seed);
}



void Game::runFixedUpdate(const double fixedStep, double& accumulator) {
#ifdef MECRAFT_DEBUG
    const auto inputStart = std::chrono::steady_clock::now();
#endif
    m_input.update();
    const InputSnapshot& inputSnapshot = m_input.snapshot();
#ifdef MECRAFT_DEBUG
    const auto inputEnd = std::chrono::steady_clock::now();
    const auto stateStart = std::chrono::steady_clock::now();
#endif

    // G5: Delegate to orchestrator
    m_frameOrchestrator->runFixedUpdate(m_session, m_stateMachine, fixedStep, accumulator);

    m_stateMachine.update(static_cast<float>(fixedStep), inputSnapshot);
#ifdef MECRAFT_DEBUG
    const auto stateEnd = std::chrono::steady_clock::now();
#endif

#ifdef MECRAFT_DEBUG
    if (m_debug) {
        m_debug->profiler.recordFixedInput(std::chrono::duration<double, std::milli>(inputEnd - inputStart).count());
        m_debug->profiler.recordFixedState(std::chrono::duration<double, std::milli>(stateEnd - stateStart).count());
        m_debug->profiler.recordFixedParticle(0.0);
        m_debug->profiler.recordFixedDrop(0.0);
        m_debug->profiler.recordFixedWorld(0.0);  // World update is now inside orchestrator
        m_debug->profiler.incrementFixedStep();
    }
#endif
}

void Game::syncAudioListener(const float deltaTime) {
    // G5: Delegate to orchestrator
    if (m_audioSyncSystem) {
        m_frameOrchestrator->syncAudioListener(*m_audioSyncSystem, deltaTime, m_session.gameplayScene().registry());
    } else {
        // Fallback during early init before audioSyncSystem is created
        AudioListenerSyncSystem fallback(m_bgmSystem, m_audioEngine);
        m_frameOrchestrator->syncAudioListener(fallback, deltaTime, m_session.gameplayScene().registry());
    }
}

void Game::renderFrame(const float frameTime) {
    if (!m_initialized) {
        return;
    }

#ifdef MECRAFT_DEBUG
    // G7: Publish debug profiler data and pass to orchestrator for dashboard
    publishDebugFrameProfiler(frameTime);
    m_frameOrchestrator->setDebugProfilerStats(m_debug ? &m_debug->dashboardProfilerStats : nullptr);
#endif

    // G5: Delegate frame rendering to orchestrator
    m_frameOrchestrator->renderFrame(m_session, *m_renderRuntime,
                                     m_stateMachine,
                                     m_hudPresenter.get(),
                                     m_window, frameTime);
}

#ifdef MECRAFT_DEBUG
void Game::publishDebugFrameProfiler(const double frameTime) {
    if (!m_debug) {
        return;
    }
    // Delegate timing accumulation, smoothing, and history to DebugFrameProfiler
    m_debug->profiler.publish(frameTime);

    const auto& timing = m_debug->profiler.timing();
    const auto& history = m_debug->profiler.history();

    auto& stats = m_debug->dashboardProfilerStats;
    stats.frameMs = frameTime * 1000.0;
    stats.fixedUpdateMs = timing.fixedUpdateMs;
    stats.fixedInputMs = timing.fixedInputMs;
    stats.fixedStateUpdateMs = timing.fixedStateUpdateMs;
    stats.fixedParticleUpdateMs = timing.fixedParticleUpdateMs;
    stats.fixedDropUpdateMs = timing.fixedDropUpdateMs;
    stats.fixedWorldUpdateMs = timing.fixedWorldUpdateMs;
    stats.audioMs = timing.audioMs;
    stats.renderMs = timing.renderMs;
    stats.fixedHistoryCount = history.count;

    // Copy history ring buffer entries in chronological order for the dashboard
    const size_t srcSize = DebugFrameProfiler::kHistorySamples;
    const auto& h = history;
    auto copyHistory = [&h, srcSize](const float* src, float* dst, size_t dstSize) {
        std::fill(dst, dst + dstSize, 0.0f);
        for (size_t i = 0; i < h.count; ++i) {
            dst[i] = src[(h.writeIndex + srcSize - h.count + i) % srcSize];
        }
    };
    copyHistory(history.fixedUpdateHistory.data(), stats.fixedUpdateHistory.data(), Dashboard::FrameProfilerStats::kFixedHistorySamples);
    copyHistory(history.fixedInputHistory.data(), stats.fixedInputHistory.data(), Dashboard::FrameProfilerStats::kFixedHistorySamples);
    copyHistory(history.fixedStateHistory.data(), stats.fixedStateHistory.data(), Dashboard::FrameProfilerStats::kFixedHistorySamples);
    copyHistory(history.fixedParticleHistory.data(), stats.fixedParticleHistory.data(), Dashboard::FrameProfilerStats::kFixedHistorySamples);
    copyHistory(history.fixedDropHistory.data(), stats.fixedDropHistory.data(), Dashboard::FrameProfilerStats::kFixedHistorySamples);
    copyHistory(history.fixedWorldHistory.data(), stats.fixedWorldHistory.data(), Dashboard::FrameProfilerStats::kFixedHistorySamples);
}
#endif

void Game::shutdown() {
    if (!m_initialized) {
        return;
    }
    m_renderRuntime->shutdown();
    m_session.shutdown();
#ifdef MECRAFT_DEBUG
    m_debug.reset();
#endif
    m_initialized = false;
}
