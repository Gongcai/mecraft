//
// Created by Caiwe on 2026/3/21.
//

#ifndef MECRAFT_GAME_H
#define MECRAFT_GAME_H
#include <array>
#include <cstddef>
#include <iostream>
#include <memory>

#include "../engine/camera/Camera.h"
#include "../engine/input/InputManager.h"
#include "../engine/platform/Window.h"
#include "../engine/platform/Time.h"

#include "../renderer/core/Renderer.h"
#include "../renderer/core/RenderScene.h"
#include "../renderer/renderers/DropRenderer.h"
#include "../renderer/renderers/FirstPersonHeldItemRenderer.h"
#include "../renderer/renderers/PostProcessRenderer.h"
#include "../renderer/renderers/HumanoidRenderer.h"
#include "camera/CameraController.h"
#include "../engine/input/InputContextManager.h"
#include "../player/ActionMap.h"
#include "states/GameStateMachine.h"
#include "../world/World.h"
#include "../world/DropSystem.h"
#include "../physics/PhysicsSystem.h"
#ifdef MECRAFT_DEBUG
#include "../ui/Dashboard.h"
#endif
#include "../ui/core/UIRenderer.h"
#include "../audio/AudioEngine.h"
#include "../audio/BgmSystem.h"
#include "../particle/ParticleSystem.h"
#include "../particle/RainRenderer.h"
#include "../crafting/CraftingSystem.h"
#include "../ecs/GameplayScene.h"
#include "states/StateDependencies.h"
#include "../locale/LocaleManager.h"
#include "session/GameSessionConfig.h"
#include "session/GameSession.h"
#include "presentation/GameplayPresentationBuilder.h"
#include "presentation/GameplayHudPresenter.h"
#include "audio/AudioListenerSyncSystem.h"

/// Legacy init params (kept for backward compatibility with GameplayAppState).
struct GameInitParams {
    Window* window = nullptr;
    InputManager* input = nullptr;
    ActionMap* actionMap = nullptr;
    InputContextManager* contextManager = nullptr;
    ResourceMgr* resourceMgr = nullptr;
    AudioEngine* audioEngine = nullptr;
    BgmSystem* bgmSystem = nullptr;
    UIRenderer* uiRenderer = nullptr;
    LocaleManager* localeManager = nullptr;
    int seed = 1234;
};

class Game {

public:
    /// Legacy constructor (kept for backward compatibility with GameplayAppState).
    explicit Game(const GameInitParams& params);

    /// New constructor using structured config and dependencies.
    Game(GameSessionConfig config, GameSessionDependencies deps);

    void init();
    void shutdown();

    void runFixedUpdate(double fixedStep, double& accumulator);
    void syncAudioListener(float deltaTime);
    void renderFrame(float frameTime);

    [[nodiscard]] bool isQuitToMenuRequested() const { return m_stateMachine.isQuitToMenuRequested(); }
    void clearQuitToMenuRequest() { m_stateMachine.clearQuitToMenuRequest(); }

private:
    static constexpr double TICK_RATE = 1.0 / 60.0;

    // G1: Structured config and dependencies (replaces individual member references)
    GameSessionConfig m_config;
    GameSessionDependencies m_deps;

    // External service references (from GameSessionDependencies)
    GameInitParams m_params;
    Window& m_window;
    InputManager& m_input;
    ActionMap& m_actionMap;
    InputContextManager& m_contextManager;
    ResourceMgr& m_resourceMgr;
    AudioEngine& m_audioEngine;
    BgmSystem& m_bgmSystem;
    UIRenderer& m_uiRenderer;
    LocaleManager& m_localeManager;

    // G4: Game session (owns World, ECS, physics, crafting, particles, camera, presentation)
    GameSession m_session;

    // Renderers and state (session-level but currently owned by Game)
    GameStateMachine m_stateMachine;
    Renderer      m_renderer;
    RenderScene   m_renderScene;
    DropRenderer  m_dropRenderer;
    FirstPersonHeldItemRenderer m_firstPersonHeldItemRenderer;
    HumanoidRenderer  m_humanoidRenderer;
    PostProcessRenderer m_postProcessRenderer;

    // G3: Audio and HUD systems
    std::unique_ptr<GameplayHudPresenter> m_hudPresenter;
    std::unique_ptr<AudioListenerSyncSystem> m_audioSyncSystem;

    // Legacy members (will be removed when Game fully delegates to GameSession)
    World         m_world;
    physics::PhysicsSystem m_physicsSystem;
    ParticleSystem m_particleSystem;
    RainRenderer m_rainRenderer;
    DropSystem m_dropSystem;
    CraftingSystem m_craftingSystem;
    ecs::GameplayScene m_gameplayScene;
    GameplayPresentationBuilder m_presentationBuilder;

    std::string m_lastSubmittedCommand;
#ifdef MECRAFT_DEBUG
    Dashboard      m_dashboard;
#endif


    [[nodiscard]] StateDependencies makeStateDependencies();

    void initWorld();
    void initRenderers();
    void initECS();

    /// Render precipitation particles (rain/snow) after opaque geometry.
    void renderPrecipitation(const Camera& camera, float cameraRainVisibility, float frameTime);

    /// Render held item (first/third person) with shadow data.
    void renderHeldItem(const Inventory& inventory, const HeldItemPreviewMotion& motion);

    /// Render UI overlay (player stats, dashboard, state machine).
    void renderUI(ecs::GameplayRegistry& reg, const Inventory& inventory,
                  const HeldItemPreviewMotion& motion, const Camera& camera);

    // Camera controller (first/third person)
    CameraController m_cameraController;
    bool m_initialized = false;

#ifdef MECRAFT_DEBUG
    struct FrameProfilerDebug {
        double fixedUpdateMs = 0.0;
        double fixedInputMs = 0.0;
        double fixedStateUpdateMs = 0.0;
        double fixedParticleUpdateMs = 0.0;
        double fixedDropUpdateMs = 0.0;
        double fixedWorldUpdateMs = 0.0;
        double audioMs = 0.0;
        double renderMs = 0.0;

        double fixedInputAccumMs = 0.0;
        double fixedStateAccumMs = 0.0;
        double fixedParticleAccumMs = 0.0;
        double fixedDropAccumMs = 0.0;
        double fixedWorldAccumMs = 0.0;
        size_t fixedStepCount = 0;

        size_t historyCount = 0;
        size_t historyWriteIndex = 0;
        std::array<float, Dashboard::FrameProfilerStats::kFixedHistorySamples> fixedUpdateHistory{};
        std::array<float, Dashboard::FrameProfilerStats::kFixedHistorySamples> fixedInputHistory{};
        std::array<float, Dashboard::FrameProfilerStats::kFixedHistorySamples> fixedStateHistory{};
        std::array<float, Dashboard::FrameProfilerStats::kFixedHistorySamples> fixedParticleHistory{};
        std::array<float, Dashboard::FrameProfilerStats::kFixedHistorySamples> fixedDropHistory{};
        std::array<float, Dashboard::FrameProfilerStats::kFixedHistorySamples> fixedWorldHistory{};

        double publishAccumulator = 0.0;
        double publishInterval = 0.25;
    };

    FrameProfilerDebug m_frameProfilerDebug{};
    Dashboard::FrameProfilerStats m_dashboardProfilerStats{};
    void publishDebugFrameProfiler(double frameTime);
#endif
};


#endif //MECRAFT_GAME_H
