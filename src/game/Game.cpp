//
// Created by Caiwe on 2026/3/21.
//
#include "Game.h"
#include "states/GameplayState.h"
#include "session/GameSessionDependencies.h"
#include "../world/block/Block.h"
#include "../item/Item.h"
#include "../audio/AudioListener.h"
#include "../ecs/entity/SteveModelFactory.h"
#include "../ecs/entity/MobModelFactory.h"
#include "../ecs/components/Components.h"
#include "../ecs/util/PlayerQuery.h"
#include "../ecs/util/GameplayRuntimeContext.h"
#include "../ecs/GameplayScene.h"
#include "../world/World.h"
#include "../physics/PhysicsSystem.h"
#include "../world/DropSystem.h"
#include "../particle/ParticleSystem.h"
#include "../particle/RainRenderer.h"
#include "../crafting/CraftingSystem.h"
#include "camera/CameraController.h"
#include "presentation/GameplayPresentationBuilder.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/vec3.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

#ifdef MECRAFT_DEBUG
#include <chrono>
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
      m_localeManager(*params.localeManager) {
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
      m_localeManager(m_deps.localeManager) {
}

void Game::init() {
    if (m_initialized) {
        return;
    }
    m_initialized = true;
    m_session.init(m_config, m_resourceMgr, m_renderer.getThreadPool());
    initWorld();
    initRenderers();
    initECS();

    // G3: Initialize audio sync and HUD presenter
    m_audioSyncSystem = std::make_unique<AudioListenerSyncSystem>(m_bgmSystem, m_audioEngine);
    m_hudPresenter = std::make_unique<GameplayHudPresenter>(m_window, m_uiRenderer, m_input);

    m_stateMachine.pushState(std::make_unique<GameplayState>(makeStateDependencies()));

#ifdef MECRAFT_DEBUG
    m_dashboard.init(m_window);
    m_dashboard.setFirstPersonHeldItemRenderer(&m_firstPersonHeldItemRenderer);
    // Inject Dashboard into presenter (Game owns, presenter renders)
    m_hudPresenter->setDashboard(&m_dashboard);
#endif
}


// initWindow and initResources removed

void Game::initWorld() {
    m_session.initWorld(m_config.seed);
}

void Game::initRenderers() {
    m_renderer.init(m_resourceMgr);

    // Initialize RenderScene and connect to Renderer
    m_renderScene.init(m_resourceMgr);
    m_renderScene.initFromRenderer(&m_renderer);

    // Enable fog via RenderSettings
    RenderSettings settings = m_renderScene.getSettings();
    settings.fog.enabled = true;
    m_renderScene.setSettings(settings);

    m_dropRenderer.init(m_resourceMgr);
    m_firstPersonHeldItemRenderer.init(m_resourceMgr);
    m_humanoidRenderer.init(m_resourceMgr);
    m_renderer.setHumanoidRenderer(&m_humanoidRenderer);
    m_renderer.setDropRenderer(&m_dropRenderer);
    m_renderer.setDropSystem(&m_session.dropSystem());
    m_renderer.setGameplayRegistry(&m_session.gameplayScene().registry());
    m_renderer.setParticleSystem(&m_session.particleSystem());
    m_renderScene.setHumanoidRenderer(&m_humanoidRenderer);
    m_renderScene.setDropRenderer(&m_dropRenderer);
    m_renderScene.setDropSystem(&m_session.dropSystem());
    m_renderScene.setGameplayRegistry(&m_session.gameplayScene().registry());
    m_renderScene.setParticleSystem(&m_session.particleSystem());
    m_uiRenderer.setHumanoidRenderer(&m_humanoidRenderer);
    m_postProcessRenderer.init(m_resourceMgr);
    m_session.particleSystem().init(m_resourceMgr);
    m_session.rainRenderer().init(m_resourceMgr);

    glEnable(GL_DEPTH_TEST);
}

// initAudio removed

void Game::initECS() {
    // G4: Delegate ECS initialization to GameSession
    ExternalEcsServices ext;
    ext.audioEngine = &m_audioEngine;
    ext.inputContextManager = &m_contextManager;
    ext.resourceMgr = &m_resourceMgr;
    ext.uiRenderer = &m_uiRenderer;
    ext.localeManager = &m_localeManager;
    m_session.initECS(ext);
}

// clampFrameTime removed

StateDependencies Game::makeStateDependencies() {
    return {
        m_stateMachine,
        m_session.getPlayerInventory(),
        m_contextManager,
        m_input,
        m_uiRenderer,
        m_lastSubmittedCommand,
        m_session.physicsSystem(),
        m_session.world(),
        m_audioEngine,
        m_session.particleSystem(),
        m_session.dropSystem(),
        m_session.gameplayScene().registry(),
        m_localeManager
    };
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
    m_frameOrchestrator.runFixedUpdate(m_session, m_stateMachine, fixedStep, accumulator);

    m_stateMachine.update(static_cast<float>(fixedStep), inputSnapshot);
#ifdef MECRAFT_DEBUG
    const auto stateEnd = std::chrono::steady_clock::now();
#endif

#ifdef MECRAFT_DEBUG
    m_debugProfiler.recordFixedInput(std::chrono::duration<double, std::milli>(inputEnd - inputStart).count());
    m_debugProfiler.recordFixedState(std::chrono::duration<double, std::milli>(stateEnd - stateStart).count());
    m_debugProfiler.recordFixedParticle(0.0);
    m_debugProfiler.recordFixedDrop(0.0);
    m_debugProfiler.recordFixedWorld(0.0);  // World update is now inside orchestrator
    m_debugProfiler.incrementFixedStep();
#endif
}

void Game::syncAudioListener(const float deltaTime) {
    // G5: Delegate to orchestrator
    if (m_audioSyncSystem) {
        m_frameOrchestrator.syncAudioListener(*m_audioSyncSystem, deltaTime, m_session.gameplayScene().registry());
    } else {
        // Fallback during early init before audioSyncSystem is created
        m_bgmSystem.update(deltaTime);
        m_audioEngine.update(deltaTime);
        ecs::PlayerQuery query(m_session.gameplayScene().registry());
        AudioListener::setPosition(query.getEyePosition());
        AudioListener::setOrientation(
            query.getCameraFront(),
            query.getCameraUp()
        );
    }
}

void Game::renderFrame(const float frameTime) {
    if (!m_initialized) {
        return;
    }

#ifdef MECRAFT_DEBUG
    // G7: Publish debug profiler data and pass to orchestrator for dashboard
    publishDebugFrameProfiler(frameTime);
    m_frameOrchestrator.setDebugProfilerStats(&m_dashboardProfilerStats);
#endif

    // G5: Delegate frame rendering to orchestrator
    m_frameOrchestrator.renderFrame(m_session, m_renderer, m_renderScene, m_stateMachine,
                                    m_postProcessRenderer, m_hudPresenter.get(),
                                    m_window, frameTime);
}

#ifdef MECRAFT_DEBUG
void Game::publishDebugFrameProfiler(const double frameTime) {
    // Delegate timing accumulation, smoothing, and history to DebugFrameProfiler
    m_debugProfiler.publish(frameTime);

    const auto& timing = m_debugProfiler.timing();
    const auto& history = m_debugProfiler.history();

    m_dashboardProfilerStats.frameMs = frameTime * 1000.0;
    m_dashboardProfilerStats.fixedUpdateMs = timing.fixedUpdateMs;
    m_dashboardProfilerStats.fixedInputMs = timing.fixedInputMs;
    m_dashboardProfilerStats.fixedStateUpdateMs = timing.fixedStateUpdateMs;
    m_dashboardProfilerStats.fixedParticleUpdateMs = timing.fixedParticleUpdateMs;
    m_dashboardProfilerStats.fixedDropUpdateMs = timing.fixedDropUpdateMs;
    m_dashboardProfilerStats.fixedWorldUpdateMs = timing.fixedWorldUpdateMs;
    m_dashboardProfilerStats.audioMs = timing.audioMs;
    m_dashboardProfilerStats.renderMs = timing.renderMs;
    m_dashboardProfilerStats.fixedHistoryCount = history.count;

    // Copy history ring buffer entries in chronological order for the dashboard
    const size_t srcSize = DebugFrameProfiler::kHistorySamples;
    const auto& h = history;
    auto copyHistory = [&h, srcSize](const float* src, float* dst, size_t dstSize) {
        std::fill(dst, dst + dstSize, 0.0f);
        for (size_t i = 0; i < h.count; ++i) {
            dst[i] = src[(h.writeIndex + srcSize - h.count + i) % srcSize];
        }
    };
    copyHistory(history.fixedUpdateHistory.data(), m_dashboardProfilerStats.fixedUpdateHistory.data(), Dashboard::FrameProfilerStats::kFixedHistorySamples);
    copyHistory(history.fixedInputHistory.data(), m_dashboardProfilerStats.fixedInputHistory.data(), Dashboard::FrameProfilerStats::kFixedHistorySamples);
    copyHistory(history.fixedStateHistory.data(), m_dashboardProfilerStats.fixedStateHistory.data(), Dashboard::FrameProfilerStats::kFixedHistorySamples);
    copyHistory(history.fixedParticleHistory.data(), m_dashboardProfilerStats.fixedParticleHistory.data(), Dashboard::FrameProfilerStats::kFixedHistorySamples);
    copyHistory(history.fixedDropHistory.data(), m_dashboardProfilerStats.fixedDropHistory.data(), Dashboard::FrameProfilerStats::kFixedHistorySamples);
    copyHistory(history.fixedWorldHistory.data(), m_dashboardProfilerStats.fixedWorldHistory.data(), Dashboard::FrameProfilerStats::kFixedHistorySamples);
}
#endif

void Game::shutdown() {
    if (!m_initialized) {
        return;
    }
    m_postProcessRenderer.shutdown();
    m_humanoidRenderer.shutdown();
    m_firstPersonHeldItemRenderer.shutdown();
    m_dropRenderer.shutdown();
    m_renderer.shutdown();
    m_session.shutdown();
    m_initialized = false;
}
