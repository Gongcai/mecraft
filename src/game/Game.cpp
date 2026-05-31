//
// Created by Caiwe on 2026/3/21.
//
#include "Game.h"
#include "Paths.h"
#include "states/GameplayState.h"
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
#endif
}


// initWindow and initResources removed

void Game::initWorld() {
    m_session.world().init(m_config.seed);
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
    auto& svc = m_session.gameplayScene().services();
    svc.world              = &m_session.world();
    svc.audioEngine        = &m_audioEngine;
    svc.inputContextManager = &m_contextManager;
    svc.resourceMgr        = &m_resourceMgr;
    svc.dropSystem         = &m_session.dropSystem();
    svc.particleSystem     = &m_session.particleSystem();
    svc.uiRenderer         = &m_uiRenderer;
    svc.physicsSystem      = &m_session.physicsSystem();
    svc.cameraController   = &m_session.cameraController();

    // UIRenderer is initialized in GameManager
    m_session.craftingSystem().loadRecipes(RECIPES_CONFIG_PATH);
    m_uiRenderer.setCraftingSystem(&m_session.craftingSystem());

    auto& reg = m_session.gameplayScene().registry();
    m_session.dropSystem().bindRegistry(reg);
    m_session.dropSystem().bindServices(svc);
    m_session.particleSystem().bindRegistry(reg);

    constexpr float kSpawnHeightOffset = 2.0f;

    const glm::vec3 spawnPos(0.0f,
        static_cast<float>(m_session.world().getSurfaceY(0, 0) + kSpawnHeightOffset), 0.0f);

    m_session.gameplayScene().initLocalPlayer(spawnPos);

    ecs::PlayerQuery query(reg);
    auto steveRoot = ecs::SteveModelFactory::createSteve(reg, query.getPosition());
    reg.emplace<ecs::SkinTypeComponent>(steveRoot, ecs::SkinTypeComponent::Type::Player);
    auto playerView = reg.view<ecs::LocalPlayerTag, ecs::TransformComponent>();
    for (auto e : playerView) {
        auto& playerTransform = reg.get<ecs::TransformComponent>(e);
        auto& steveAnim = reg.get<ecs::SteveAnimationStateComponent>(steveRoot);
        steveAnim.lastPosition = playerTransform.position;
    }

#ifdef MECRAFT_DEBUG
    constexpr float kTestMobOffsetX = 5.0f;
    glm::vec3 playerPos = query.getPosition();
    ecs::MobModelFactory::createZombie(reg, glm::vec3(playerPos.x + kTestMobOffsetX, playerPos.y, playerPos.z));
#endif
}

// clampFrameTime removed

StateDependencies Game::makeStateDependencies() {
    // Get inventory from ECS
    auto& reg = m_session.gameplayScene().registry();
    Inventory* inventory = nullptr;
    auto view = reg.view<ecs::LocalPlayerTag, ecs::InventoryDataComponent>();
    for (auto e : view) {
        inventory = &view.get<ecs::InventoryDataComponent>(e).inventory;
        break;
    }
    if (inventory == nullptr) {
        throw std::runtime_error("Local player inventory is not initialized.");
    }

    return {
        m_stateMachine,
        *inventory,
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

    // G5: Delegate frame rendering to orchestrator
    m_frameOrchestrator.renderFrame(m_session, m_renderer, m_renderScene, m_stateMachine,
                                    m_postProcessRenderer, m_hudPresenter.get(), m_input,
                                    m_uiRenderer, m_window, frameTime);
}

void Game::renderPrecipitation(const Camera& camera, float cameraRainVisibility, float frameTime) {
    const auto& settings = m_renderScene.getSettings();
    if (!settings.weather.rainLinesEnabled) return;

    const auto& weather = m_session.world().getWeatherSystem().getDerived();
    const glm::vec3 camPos = camera.getPosition();
    auto projMat = camera.getProjectionMatrix(m_window.getAspectRatio());
    auto viewMat = camera.getViewMatrix();
    float alphaScale = settings.weather.rainAlphaScale;
    const bool forwardVanillaActive = m_renderScene.isNewPipelineActive() &&
                                      m_renderScene.getPipelineMode() == PipelineMode::Forward;
    const auto& frameOutput = m_renderScene.getLastFrameOutput();
    const GLuint depthTex = forwardVanillaActive ? 0 : frameOutput.gbufferDepthTex;
    const bool hardwareDepthTest = !m_renderScene.isNewPipelineActive() || forwardVanillaActive;
    const glm::vec2 precipitationScreenSize(
        static_cast<float>(std::max(1, m_window.getWidth())),
        static_cast<float>(std::max(1, m_window.getHeight())));

    if (weather.rainStrength > 0.01f) {
        m_session.rainRenderer().render(projMat, viewMat, camPos,
                              weather.rainStrength, cameraRainVisibility,
                              alphaScale, depthTex,
                              precipitationScreenSize, frameTime,
                              hardwareDepthTest);
    }
    if (weather.snowStrength > 0.01f) {
        m_session.rainRenderer().renderSnow(projMat, viewMat, camPos,
                                  weather.snowStrength, cameraRainVisibility,
                                  alphaScale * 0.6f, depthTex,
                                  precipitationScreenSize, frameTime,
                                  hardwareDepthTest);
    }
}

void Game::renderHeldItem(const Inventory& inventory, const HeldItemPreviewMotion& motion) {
    if (m_session.cameraController().isFirstPerson()) {
        const bool forwardVanillaActive = m_renderScene.isNewPipelineActive() &&
                                          m_renderScene.isNewPipelineReady() &&
                                          m_renderScene.getPipelineMode() == PipelineMode::Forward;
        m_firstPersonHeldItemRenderer.setForwardMode(forwardVanillaActive);
        if (m_uiRenderer.consumeHeldItemPreviewSwingTrigger()) {
            m_firstPersonHeldItemRenderer.triggerSwing();
        }
        m_firstPersonHeldItemRenderer.setContinuousSwing(m_uiRenderer.isHeldItemPreviewActionAnimationActive());
        m_firstPersonHeldItemRenderer.setShadowData(
            FirstPersonHeldItemRenderer::fromFirstPersonShadowData(m_renderScene.getHeldItemShadowData()));
        m_firstPersonHeldItemRenderer.render(m_window, inventory, motion,
                                             static_cast<float>(Time::getGameTime()));
    } else {
        static_cast<void>(m_uiRenderer.consumeHeldItemPreviewSwingTrigger());
        m_firstPersonHeldItemRenderer.setContinuousSwing(false);
    }
}

void Game::renderUI(ecs::GameplayRegistry& reg, const Inventory& inventory,
                    const HeldItemPreviewMotion& motion, const Camera& camera) {
    // G3: Build player stats from snapshot (already computed in renderFrame)
    // Note: This still queries ECS for now; will be fully snapshot-driven in G4
    PlayerStatsData playerStats;
    ecs::PlayerQuery playerQuery(reg);
    playerStats.health = playerQuery.getHealth();
    playerStats.maxHealth = playerQuery.getMaxHealth();
    playerStats.armor = playerQuery.getArmor();
    playerStats.maxArmor = playerQuery.getMaxArmor();
    playerStats.food = playerQuery.getFood();
    playerStats.maxFood = playerQuery.getMaxFood();

    if (reg.ctxHas<ecs::GameplayRuntimeContext>()) {
        playerStats.showSurvivalStats = reg.ctxGet<ecs::GameplayRuntimeContext>().gameplayMode != GameplayMode::Creative;
    }

    m_uiRenderer.render(m_window, inventory, playerStats, motion, m_input.snapshot());
    m_stateMachine.render();
#ifdef MECRAFT_DEBUG
    Camera mutableCamera = camera;
    m_dashboard.render(reg, m_session.world(), mutableCamera, m_renderer, m_renderScene,
                       m_postProcessRenderer, m_uiRenderer, m_dashboardProfilerStats);
#endif
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
    const size_t historySize = DebugFrameProfiler::kHistorySamples;
    const auto copyHistory = [&](const std::array<float, DebugFrameProfiler::kHistorySamples>& src,
                                 std::array<float, Dashboard::FrameProfilerStats::kFixedHistorySamples>& dst) {
        dst.fill(0.0f);
        for (size_t i = 0; i < history.count; ++i) {
            dst[i] = src[(history.writeIndex + historySize - history.count + i) % historySize];
        }
    };
    copyHistory(history.fixedUpdateHistory, m_dashboardProfilerStats.fixedUpdateHistory);
    copyHistory(history.fixedInputHistory, m_dashboardProfilerStats.fixedInputHistory);
    copyHistory(history.fixedStateHistory, m_dashboardProfilerStats.fixedStateHistory);
    copyHistory(history.fixedParticleHistory, m_dashboardProfilerStats.fixedParticleHistory);
    copyHistory(history.fixedDropHistory, m_dashboardProfilerStats.fixedDropHistory);
    copyHistory(history.fixedWorldHistory, m_dashboardProfilerStats.fixedWorldHistory);
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
