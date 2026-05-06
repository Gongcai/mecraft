//
// Created by Caiwe on 2026/3/21.
//
#include "Game.h"
#include "Paths.h"
#include "states/GameplayState.h"
#include "../world/Block.h"
#include "../item/Item.h"
#include "../audio/AudioListener.h"
#include "../ecs/entity/SteveModelFactory.h"
#include "../ecs/entity/MobModelFactory.h"
#include "../ecs/components/Components.h"
#include "../ecs/util/PlayerQuery.h"

#include <GLFW/glfw3.h>

#ifndef NDEBUG
#include <chrono>
#endif

Game::Game(const GameInitParams& params) 
    : m_params(params),
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

void Game::init() {
    initWorld();
    initRenderers();
    initECS();

    m_stateMachine.pushState(std::make_unique<GameplayState>(makeStateDependencies()));

#ifndef NDEBUG
    m_dashboard.init(m_window);
#endif
}

// initWindow and initResources removed

void Game::initWorld() {
    constexpr int kRenderDistance = 1;

    m_world.init(m_params.seed);
    m_world.setRenderDistance(kRenderDistance);
}

void Game::initRenderers() {
    m_renderer.init(m_resourceMgr);
    m_world.setThreadPool(m_renderer.getThreadPool());
    m_renderer.setFogEnabled(true);

    m_dropRenderer.init(m_resourceMgr);
    m_humanoidRenderer.init(m_resourceMgr);
    m_postProcessRenderer.init(m_resourceMgr);
    m_particleSystem.init(m_resourceMgr);

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

#ifndef NDEBUG
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
#ifndef NDEBUG
    const auto inputStart = std::chrono::steady_clock::now();
#endif
    m_input.update();
    const InputSnapshot& inputSnapshot = m_input.snapshot();
#ifndef NDEBUG
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
#ifndef NDEBUG
    const auto stateEnd = std::chrono::steady_clock::now();
    const auto worldStart = std::chrono::steady_clock::now();
#endif

    ecs::PlayerQuery query(m_gameplayScene.registry());
    m_world.update(query.getPosition());
#ifndef NDEBUG
    const auto worldEnd = std::chrono::steady_clock::now();
#endif



#ifndef NDEBUG

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
    // Update BGM before AudioEngine cleanup so track-end detection keeps a valid source pointer.
    m_bgmSystem.update(deltaTime);
    m_audioEngine.update(deltaTime);
    ecs::PlayerQuery query(m_gameplayScene.registry());
    AudioListener::setPosition(query.getEyePosition());
    AudioListener::setOrientation(
        query.getCameraFront(),
        query.getCameraUp()
    );
}

void Game::renderFrame(const float frameTime) {
    (void)frameTime;
    // Read fall-roll radians from ECS component.
    float fallRollRadians = 0.0f;
    auto& reg = m_gameplayScene.registry();
    {
        auto view = reg.view<ecs::LocalPlayerTag, ecs::FallRollComponent>();
        for (auto e : view) {
            fallRollRadians = reg.get<ecs::FallRollComponent>(e).currentRadians;
        }
    }

    m_postProcessRenderer.beginScene(m_window);

    // Build render camera from ECS state
    Camera renderCamera;
    glm::vec3 eyePosition(0.0f);
    {
        ecs::PlayerQuery query(reg);
        auto camView = reg.view<ecs::LocalPlayerTag, ecs::CameraStateComponent>();
        auto transformView = reg.view<ecs::LocalPlayerTag, ecs::TransformComponent>();
        auto viewBobView = reg.view<ecs::LocalPlayerTag, ecs::ViewBobComponent>();

        for (auto e : camView) {
            const auto& cam = camView.get<ecs::CameraStateComponent>(e);
            const auto& transform = transformView.get<ecs::TransformComponent>(e);
            const auto& viewBob = viewBobView.get<ecs::ViewBobComponent>(e);

            renderCamera.setYawPitch(cam.yaw, cam.pitch);
            renderCamera.setFOV(cam.fov);

            // Eye position with view bob offsets
            eyePosition = transform.position +
                glm::vec3(0.0f, transform.eyeHeight + viewBob.verticalOffset, 0.0f);

            // Apply horizontal bob
            glm::vec3 right = cam.right;
            right.y = 0.0f;
            if (glm::length(right) > 0.001f) {
                right = glm::normalize(right);
            } else {
                right = glm::vec3(1.0f, 0.0f, 0.0f);
            }
            eyePosition += right * viewBob.horizontalOffset;

            renderCamera.setPosition(eyePosition);
            break;
        }
    }

    Camera finalCamera = m_cameraController.computeRenderCamera(renderCamera, eyePosition);

    m_renderer.renderOpaqueAndCutout(m_world, finalCamera, m_window);
    m_dropRenderer.render(m_dropSystem, finalCamera, m_window);

    if (m_cameraController.shouldRenderPlayerModel()) {
        m_humanoidRenderer.render(m_gameplayScene.registry(), finalCamera, m_window,
                                  HumanoidRenderer::kRenderAll);
    } else {
        m_humanoidRenderer.render(m_gameplayScene.registry(), finalCamera, m_window,
                                  HumanoidRenderer::kRenderMobsOnly);
    }
    m_particleSystem.render(finalCamera.getProjectionMatrix(m_window.getAspectRatio()),
                            finalCamera.getViewMatrix());

    // Read block interaction data from ECS and pass to Renderer
    BlockTargetRenderData targetData;
    BlockBreakRenderData breakData;
    {
        ecs::PlayerQuery playerQuery(reg);
        targetData.hasTarget = playerQuery.hasTargetBlock();
        targetData.targetBlock = playerQuery.getTargetBlock();
        breakData.active = playerQuery.hasBlockBreakProgress();
        breakData.progress01 = playerQuery.getBlockBreakProgress();
        breakData.blockPos = playerQuery.getBreakTargetBlock();
    }
    m_renderer.renderTransparentAndOverlays(m_world, targetData, breakData, m_window);

    ecs::PlayerQuery playerQuery(reg);
    PostProcessEffects effects;
    effects.underwaterEnabled = playerQuery.isEyesInWater();
    effects.screenRollRadians = fallRollRadians;
    m_postProcessRenderer.setEffects(effects);
    m_postProcessRenderer.endSceneAndComposite(m_window);

    PlayerStatsData playerStats;
    playerStats.health = playerQuery.getHealth();
    playerStats.maxHealth = playerQuery.getMaxHealth();
    playerStats.armor = playerQuery.getArmor();
    playerStats.maxArmor = playerQuery.getMaxArmor();
    playerStats.food = playerQuery.getFood();
    playerStats.maxFood = playerQuery.getMaxFood();

    HeldItemPreviewMotion heldItemMotion;
    heldItemMotion.moving = playerQuery.isMoving();
    heldItemMotion.sprinting = playerQuery.isSprinting();
    heldItemMotion.bobFrequency = playerQuery.getEyeBobFrequency();
    heldItemMotion.bobPhaseOffset = playerQuery.getEyeBobPhaseOffset();

    // Get inventory from ECS
    const Inventory& inventory = playerQuery.getInventory();

    m_uiRenderer.render(m_window, inventory, playerStats, heldItemMotion, m_input.snapshot());
    m_stateMachine.render();
#ifndef NDEBUG
    m_dashboard.render(reg, m_world, finalCamera, m_renderer, m_uiRenderer,
                       m_dashboardProfilerStats);
#endif
    m_window.swapBuffers();
}

#ifndef NDEBUG
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
    m_particleSystem.shutdown();
    m_postProcessRenderer.shutdown();
    m_humanoidRenderer.shutdown();
    m_dropRenderer.shutdown();
    m_renderer.shutdown();
}
