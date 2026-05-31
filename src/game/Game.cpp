//
// Created by Caiwe on 2026/3/21.
//
#include "Game.h"
#include "states/GameStateMachine.h"
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

// DebugRuntime struct has been migrated to GameplayRenderRuntime

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
    m_session.initStateMachine(m_deps);

    // G3: Initialize audio sync and HUD presenter
    m_audioSyncSystem = std::make_unique<AudioListenerSyncSystem>(m_bgmSystem, m_audioEngine);
    m_hudPresenter = std::make_unique<GameplayHudPresenter>(m_window, m_uiRenderer, m_input);

#ifdef MECRAFT_DEBUG
    m_renderRuntime->initDebug(m_window);
    // Inject Dashboard into presenter (renderRuntime owns, presenter renders)
    m_hudPresenter->setDashboard(m_renderRuntime->dashboard());
#endif
}


void Game::initWorld() {
    m_session.initWorld(m_config.seed);
}



void Game::runFixedUpdate(const double fixedStep, double& accumulator) {
    m_frameOrchestrator->runFixedUpdate(m_session, m_input, m_renderRuntime.get(), fixedStep, accumulator);
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
    m_renderRuntime->publishDebugStats(frameTime);
#endif

    // G5: Delegate frame rendering to orchestrator
    m_frameOrchestrator->renderFrame(m_session, *m_renderRuntime,
                                     m_hudPresenter.get(),
                                     m_window, frameTime);
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
