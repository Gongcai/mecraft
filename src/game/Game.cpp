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
      m_localeManager(*params.localeManager),
      m_physicsSystem(&m_world) {
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
      m_physicsSystem(&m_world) {
}

void Game::init() {
    if (m_initialized) {
        return;
    }
    m_initialized = true;
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
    constexpr int kRenderDistance = 16;

    m_world.init(m_params.seed);
    m_world.setRenderDistance(kRenderDistance);
}

void Game::initRenderers() {
    m_renderer.init(m_resourceMgr);
    m_world.setThreadPool(m_renderer.getThreadPool());
    m_renderer.setFogEnabled(true);

    // Initialize RenderScene and connect to Renderer
    m_renderScene.init(m_resourceMgr);
    m_renderScene.setLegacyRenderer(&m_renderer);

    m_dropRenderer.init(m_resourceMgr);
    m_firstPersonHeldItemRenderer.init(m_resourceMgr);
    m_humanoidRenderer.init(m_resourceMgr);
    m_renderer.setHumanoidRenderer(&m_humanoidRenderer);
    m_renderer.setDropRenderer(&m_dropRenderer);
    m_renderer.setDropSystem(&m_dropSystem);
    m_renderer.setGameplayRegistry(&m_gameplayScene.registry());
    m_renderer.setParticleSystem(&m_particleSystem);
    m_renderScene.setHumanoidRenderer(&m_humanoidRenderer);
    m_renderScene.setDropRenderer(&m_dropRenderer);
    m_renderScene.setDropSystem(&m_dropSystem);
    m_renderScene.setGameplayRegistry(&m_gameplayScene.registry());
    m_renderScene.setParticleSystem(&m_particleSystem);
    m_uiRenderer.setHumanoidRenderer(&m_humanoidRenderer);
    m_postProcessRenderer.init(m_resourceMgr);
    m_particleSystem.init(m_resourceMgr);
    m_rainRenderer.init(m_resourceMgr);

    glEnable(GL_DEPTH_TEST);
}

// initAudio removed

void Game::initECS() {
    auto& svc = m_gameplayScene.services();
    svc.world              = &m_world;
    svc.audioEngine        = &m_audioEngine;
    svc.inputContextManager = &m_contextManager;
    svc.resourceMgr        = &m_resourceMgr;
    svc.dropSystem         = &m_dropSystem;
    svc.particleSystem     = &m_particleSystem;
    svc.uiRenderer         = &m_uiRenderer;
    svc.physicsSystem      = &m_physicsSystem;
    svc.cameraController   = &m_cameraController;

    // UIRenderer is initialized in GameManager
    m_craftingSystem.loadRecipes(RECIPES_CONFIG_PATH);
    m_uiRenderer.setCraftingSystem(&m_craftingSystem);

    auto& reg = m_gameplayScene.registry();
    m_dropSystem.bindRegistry(reg);
    m_dropSystem.bindServices(svc);
    m_particleSystem.bindRegistry(reg);

    constexpr float kSpawnHeightOffset = 2.0f;

    const glm::vec3 spawnPos(0.0f,
        static_cast<float>(m_world.getSurfaceY(0, 0) + kSpawnHeightOffset), 0.0f);

    m_gameplayScene.initLocalPlayer(spawnPos);

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
    auto& reg = m_gameplayScene.registry();
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
        m_physicsSystem,
        m_world,
        m_audioEngine,
        m_particleSystem,
        m_dropSystem,
        m_gameplayScene.registry(),
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
    accumulator -= fixedStep;

    // ECS pre-state stage: sample input and build intents before states consume them.
    m_gameplayScene.runFixedUpdate(static_cast<float>(fixedStep));

    m_gameplayScene.tickClock().advance(fixedStep);
    uint32_t ticksThisFrame = 0;
    while (m_gameplayScene.tickClock().shouldTick()
           && ticksThisFrame < m_gameplayScene.tickClock().maxTicksPerFrame()) {
        m_gameplayScene.runOneTick();
        m_gameplayScene.tickClock().consumeTick();
        ++ticksThisFrame;
    }

    m_stateMachine.update(static_cast<float>(fixedStep), inputSnapshot);
    if (m_stateMachine.isQuitToMenuRequested()) {
        return;
    }
#ifdef MECRAFT_DEBUG
    const auto stateEnd = std::chrono::steady_clock::now();
    const auto worldStart = std::chrono::steady_clock::now();
#endif

    ecs::PlayerQuery query(m_gameplayScene.registry());
    m_world.update(query.getPosition());
#ifdef MECRAFT_DEBUG
    const auto worldEnd = std::chrono::steady_clock::now();
#endif



#ifdef MECRAFT_DEBUG

    m_frameProfilerDebug.fixedInputAccumMs += std::chrono::duration<double, std::milli>(inputEnd - inputStart).count();
    m_frameProfilerDebug.fixedStateAccumMs += std::chrono::duration<double, std::milli>(stateEnd - stateStart).count();
    // Particle simulation has moved into GameplayScene ECS systems.
    m_frameProfilerDebug.fixedParticleAccumMs += 0.0;
    // Drop update/collect has moved into GameplayScene ECS bridge systems.
    m_frameProfilerDebug.fixedDropAccumMs += 0.0;


    m_frameProfilerDebug.fixedWorldAccumMs += std::chrono::duration<double, std::milli>(worldEnd - worldStart).count();
    ++m_frameProfilerDebug.fixedStepCount;
#endif
}

void Game::syncAudioListener(const float deltaTime) {
    // G3: Delegate to AudioListenerSyncSystem
    if (m_audioSyncSystem) {
        m_audioSyncSystem->update(deltaTime, m_gameplayScene.registry());
    } else {
        // Fallback during early init before audioSyncSystem is created
        m_bgmSystem.update(deltaTime);
        m_audioEngine.update(deltaTime);
        ecs::PlayerQuery query(m_gameplayScene.registry());
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

    // R7: Activate new pipeline on first frame (after render targets are ready)
    if (!m_renderScene.isNewPipelineActive() && m_renderScene.isNewPipelineReady()) {
        m_renderScene.setNewPipelineActive(true);
    }

    // G2: Build presentation snapshot from ECS (single point of ECS access)
    auto& reg = m_gameplayScene.registry();
    const auto snap = m_presentationBuilder.build(reg, m_cameraController);

    // Apply snapshot state to RenderScene
    m_renderScene.setRenderLocalPlayerModel(snap.renderLocalPlayerModel);
    m_renderScene.setHeldBlockLightValue(snap.heldBlockLightLevel);
    m_renderScene.setEyeInWater(snap.eyeInWater);

    // R7: New pipeline is always active. Forward vanilla renders directly to backbuffer.
    const bool skipPostProcess = m_renderScene.getPipelineMode() == PipelineMode::Forward;
    if (!skipPostProcess) {
        m_postProcessRenderer.beginScene(m_window);
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, std::max(1, m_window.getWidth()), std::max(1, m_window.getHeight()));
    }

    const bool lightDebugActive = m_renderScene.isLightDebugActive();
    float cameraRainVisibility = 1.0f;

    // Convert snapshot types to renderer types
    BlockTargetRenderData targetData;
    targetData.hasTarget = snap.blockTarget.hasTarget;
    targetData.targetBlock = snap.blockTarget.targetBlock;
    BlockBreakRenderData breakData;
    breakData.active = snap.blockBreak.active;
    breakData.progress01 = snap.blockBreak.progress01;
    breakData.blockPos = snap.blockBreak.blockPos;

    // R7: Use new pipeline path (RenderScene handles all rendering)
    m_renderScene.renderFrame(m_world, snap.renderCamera, m_window, targetData, breakData);

    if (!lightDebugActive) {
        cameraRainVisibility = m_renderScene.computeCameraRainVisibility(m_world, snap.renderCamera.getPosition());
        renderPrecipitation(snap.renderCamera, cameraRainVisibility, frameTime);
    }

    // Phase 11: Build post-process effects via RenderScene
    if (!skipPostProcess) {
        PostProcessEffects effects = m_renderScene.buildPostProcessEffects(
            m_world, snap.renderCamera, m_window, cameraRainVisibility, snap.fallRollRadians);
        m_postProcessRenderer.setEffects(effects);
    }

    // Convert held item motion to renderer type
    HeldItemPreviewMotion heldItemMotion;
    heldItemMotion.moving = snap.heldItemMotion.moving;
    heldItemMotion.sprinting = snap.heldItemMotion.sprinting;
    heldItemMotion.bobFrequency = snap.heldItemMotion.bobFrequency;
    heldItemMotion.bobPhaseOffset = snap.heldItemMotion.bobPhaseOffset;
    heldItemMotion.cameraYawDegrees = snap.heldItemMotion.cameraYawDegrees;
    heldItemMotion.cameraPitchDegrees = snap.heldItemMotion.cameraPitchDegrees;

    // Held item rendering
    if (skipPostProcess) {
        renderHeldItem(*snap.inventory, heldItemMotion);
    } else if (lightDebugActive) {
        m_postProcessRenderer.blitSceneToBackbuffer(m_window);
    } else {
        renderHeldItem(*snap.inventory, heldItemMotion);
        m_postProcessRenderer.endSceneAndComposite(m_window, frameTime,
                                                   m_renderScene.gbufDepthTexture(),
                                                   m_renderScene.weatherMaskTexture());
    }
    m_renderScene.renderDeferredDebugOverlay(m_window);

    // UI rendering
    renderUI(reg, *snap.inventory, heldItemMotion, snap.renderCamera);
    m_window.swapBuffers();
}

void Game::renderPrecipitation(const Camera& camera, float cameraRainVisibility, float frameTime) {
    const auto& settings = m_renderScene.getSettings();
    if (!settings.weather.rainLinesEnabled) return;

    const auto& weather = m_world.getWeatherSystem().getDerived();
    const glm::vec3 camPos = camera.getPosition();
    auto projMat = camera.getProjectionMatrix(m_window.getAspectRatio());
    auto viewMat = camera.getViewMatrix();
    float alphaScale = settings.weather.rainAlphaScale;
    const bool forwardVanillaActive = m_renderScene.isNewPipelineActive() &&
                                      m_renderScene.getPipelineMode() == PipelineMode::Forward;
    const GLuint depthTex = forwardVanillaActive ? 0 : m_renderScene.gbufDepthTexture();
    const bool hardwareDepthTest = !m_renderScene.isNewPipelineActive() || forwardVanillaActive;
    const glm::vec2 precipitationScreenSize(
        static_cast<float>(std::max(1, m_window.getWidth())),
        static_cast<float>(std::max(1, m_window.getHeight())));

    if (weather.rainStrength > 0.01f) {
        m_rainRenderer.render(projMat, viewMat, camPos,
                              weather.rainStrength, cameraRainVisibility,
                              alphaScale, depthTex,
                              precipitationScreenSize, frameTime,
                              hardwareDepthTest);
    }
    if (weather.snowStrength > 0.01f) {
        m_rainRenderer.renderSnow(projMat, viewMat, camPos,
                                  weather.snowStrength, cameraRainVisibility,
                                  alphaScale * 0.6f, depthTex,
                                  precipitationScreenSize, frameTime,
                                  hardwareDepthTest);
    }
}

void Game::renderHeldItem(const Inventory& inventory, const HeldItemPreviewMotion& motion) {
    if (m_cameraController.isFirstPerson()) {
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
    m_dashboard.render(reg, m_world, mutableCamera, m_renderer, m_renderScene,
                       m_postProcessRenderer, m_uiRenderer, m_dashboardProfilerStats);
#endif
}

#ifdef MECRAFT_DEBUG
void Game::publishDebugFrameProfiler(const double frameTime) {
    const auto pushHistory = [](std::array<float, Dashboard::FrameProfilerStats::kFixedHistorySamples>& history,
                                const size_t index,
                                const double valueMs) {
        history[index] = static_cast<float>(valueMs);
    };
    const auto copyHistory = [](const std::array<float, Dashboard::FrameProfilerStats::kFixedHistorySamples>& source,
                                const size_t writeIndex,
                                const size_t count,
                                std::array<float, Dashboard::FrameProfilerStats::kFixedHistorySamples>& destination) {
        destination.fill(0.0f);
        const size_t historySize = source.size();
        for (size_t i = 0; i < count; ++i) {
            const size_t sourceIndex = (writeIndex + historySize - count + i) % historySize;
            destination[i] = source[sourceIndex];
        }
    };
    const auto smooth = [](const double previous, const double current) {
        constexpr double kAlpha = 0.15;
        return previous + (current - previous) * kAlpha;
    };

    if (m_frameProfilerDebug.fixedStepCount > 0) {
        const double invStepCount = 1.0 / static_cast<double>(m_frameProfilerDebug.fixedStepCount);
        const double inputAvgMs = m_frameProfilerDebug.fixedInputAccumMs * invStepCount;
        const double stateAvgMs = m_frameProfilerDebug.fixedStateAccumMs * invStepCount;
        const double particleAvgMs = m_frameProfilerDebug.fixedParticleAccumMs * invStepCount;
        const double dropAvgMs = m_frameProfilerDebug.fixedDropAccumMs * invStepCount;
        const double worldAvgMs = m_frameProfilerDebug.fixedWorldAccumMs * invStepCount;
        const double totalAvgMs = inputAvgMs + stateAvgMs + particleAvgMs + dropAvgMs + worldAvgMs;

        m_frameProfilerDebug.fixedInputMs = smooth(m_frameProfilerDebug.fixedInputMs, inputAvgMs);
        m_frameProfilerDebug.fixedStateUpdateMs = smooth(m_frameProfilerDebug.fixedStateUpdateMs, stateAvgMs);
        m_frameProfilerDebug.fixedParticleUpdateMs = smooth(m_frameProfilerDebug.fixedParticleUpdateMs, particleAvgMs);
        m_frameProfilerDebug.fixedDropUpdateMs = smooth(m_frameProfilerDebug.fixedDropUpdateMs, dropAvgMs);
        m_frameProfilerDebug.fixedWorldUpdateMs = smooth(m_frameProfilerDebug.fixedWorldUpdateMs, worldAvgMs);
        m_frameProfilerDebug.fixedUpdateMs = smooth(m_frameProfilerDebug.fixedUpdateMs, totalAvgMs);

        const size_t writeIndex = m_frameProfilerDebug.historyWriteIndex;
        pushHistory(m_frameProfilerDebug.fixedUpdateHistory, writeIndex, totalAvgMs);
        pushHistory(m_frameProfilerDebug.fixedInputHistory, writeIndex, inputAvgMs);
        pushHistory(m_frameProfilerDebug.fixedStateHistory, writeIndex, stateAvgMs);
        pushHistory(m_frameProfilerDebug.fixedParticleHistory, writeIndex, particleAvgMs);
        pushHistory(m_frameProfilerDebug.fixedDropHistory, writeIndex, dropAvgMs);
        pushHistory(m_frameProfilerDebug.fixedWorldHistory, writeIndex, worldAvgMs);
        m_frameProfilerDebug.historyWriteIndex = (writeIndex + 1) % m_frameProfilerDebug.fixedUpdateHistory.size();
        if (m_frameProfilerDebug.historyCount < m_frameProfilerDebug.fixedUpdateHistory.size()) {
            ++m_frameProfilerDebug.historyCount;
        }

        m_frameProfilerDebug.fixedInputAccumMs = 0.0;
        m_frameProfilerDebug.fixedStateAccumMs = 0.0;
        m_frameProfilerDebug.fixedParticleAccumMs = 0.0;
        m_frameProfilerDebug.fixedDropAccumMs = 0.0;
        m_frameProfilerDebug.fixedWorldAccumMs = 0.0;
        m_frameProfilerDebug.fixedStepCount = 0;
    }

    m_frameProfilerDebug.audioMs = smooth(m_frameProfilerDebug.audioMs, m_dashboardProfilerStats.audioMs);
    m_frameProfilerDebug.renderMs = smooth(m_frameProfilerDebug.renderMs, m_dashboardProfilerStats.renderMs);

    m_frameProfilerDebug.publishAccumulator += frameTime;
    if (m_frameProfilerDebug.publishAccumulator < m_frameProfilerDebug.publishInterval) {
        return;
    }
    m_frameProfilerDebug.publishAccumulator -= m_frameProfilerDebug.publishInterval;

    m_dashboardProfilerStats.frameMs = frameTime * 1000.0;
    m_dashboardProfilerStats.fixedUpdateMs = m_frameProfilerDebug.fixedUpdateMs;
    m_dashboardProfilerStats.fixedInputMs = m_frameProfilerDebug.fixedInputMs;
    m_dashboardProfilerStats.fixedStateUpdateMs = m_frameProfilerDebug.fixedStateUpdateMs;
    m_dashboardProfilerStats.fixedParticleUpdateMs = m_frameProfilerDebug.fixedParticleUpdateMs;
    m_dashboardProfilerStats.fixedDropUpdateMs = m_frameProfilerDebug.fixedDropUpdateMs;
    m_dashboardProfilerStats.fixedWorldUpdateMs = m_frameProfilerDebug.fixedWorldUpdateMs;
    m_dashboardProfilerStats.audioMs = m_frameProfilerDebug.audioMs;
    m_dashboardProfilerStats.renderMs = m_frameProfilerDebug.renderMs;
    m_dashboardProfilerStats.fixedHistoryCount = m_frameProfilerDebug.historyCount;
    copyHistory(m_frameProfilerDebug.fixedUpdateHistory,
                m_frameProfilerDebug.historyWriteIndex,
                m_frameProfilerDebug.historyCount,
                m_dashboardProfilerStats.fixedUpdateHistory);
    copyHistory(m_frameProfilerDebug.fixedInputHistory,
                m_frameProfilerDebug.historyWriteIndex,
                m_frameProfilerDebug.historyCount,
                m_dashboardProfilerStats.fixedInputHistory);
    copyHistory(m_frameProfilerDebug.fixedStateHistory,
                m_frameProfilerDebug.historyWriteIndex,
                m_frameProfilerDebug.historyCount,
                m_dashboardProfilerStats.fixedStateHistory);
    copyHistory(m_frameProfilerDebug.fixedParticleHistory,
                m_frameProfilerDebug.historyWriteIndex,
                m_frameProfilerDebug.historyCount,
                m_dashboardProfilerStats.fixedParticleHistory);
    copyHistory(m_frameProfilerDebug.fixedDropHistory,
                m_frameProfilerDebug.historyWriteIndex,
                m_frameProfilerDebug.historyCount,
                m_dashboardProfilerStats.fixedDropHistory);
    copyHistory(m_frameProfilerDebug.fixedWorldHistory,
                m_frameProfilerDebug.historyWriteIndex,
                m_frameProfilerDebug.historyCount,
                m_dashboardProfilerStats.fixedWorldHistory);
}
#endif

void Game::shutdown() {
    if (!m_initialized) {
        return;
    }
    m_rainRenderer.shutdown();
    m_particleSystem.shutdown();
    m_postProcessRenderer.shutdown();
    m_humanoidRenderer.shutdown();
    m_firstPersonHeldItemRenderer.shutdown();
    m_dropRenderer.shutdown();
    m_renderer.shutdown();
    m_initialized = false;
}
