//
// Created by Caiwe on 2026/3/21.
//

#ifndef MECRAFT_GAME_H
#define MECRAFT_GAME_H
#include <array>
#include <cstddef>
#include <iostream>

#include "Camera.h"
#include "InputManager.h"
#include "Window.h"
#include "Time.h"

#include "../renderer/Renderer.h"
#include "../renderer/DropRenderer.h"
#include "../renderer/PostProcessRenderer.h"
#include "../renderer/HumanoidRenderer.h"
#include "CameraController.h"
#include "InputContextManager.h"
#include "../player/ActionMap.h"
#include "GameStateMachine.h"
#include "../world/World.h"
#include "../world/DropSystem.h"
#include "../physics/PhysicsSystem.h"
#ifndef NDEBUG
#include "../ui/Dashboard.h"
#endif
#include "../ui/UIRenderer.h"
#include "../audio/AudioEngine.h"
#include "../audio/BgmSystem.h"
#include "../particle/ParticleSystem.h"
#include "../crafting/CraftingSystem.h"
#include "../ecs/GameplayScene.h"
#include "states/StateDependencies.h"
struct GameInitParams {
    Window* window = nullptr;
    InputManager* input = nullptr;
    ActionMap* actionMap = nullptr;
    InputContextManager* contextManager = nullptr;
    ResourceMgr* resourceMgr = nullptr;
    AudioEngine* audioEngine = nullptr;
    BgmSystem* bgmSystem = nullptr;
    UIRenderer* uiRenderer = nullptr;
    int seed = 1234;
};

class Game {

public:
    explicit Game(const GameInitParams& params);
    void init();
    void shutdown();

    void runFixedUpdate(double fixedStep, double& accumulator);
    void syncAudioListener(float deltaTime);
    void renderFrame(float frameTime);

private:
    static constexpr double TICK_RATE = 1.0 / 60.0;

    GameInitParams m_params;
    Window& m_window;
    InputManager& m_input;
    ActionMap& m_actionMap;
    InputContextManager& m_contextManager;
    ResourceMgr& m_resourceMgr;
    AudioEngine& m_audioEngine;
    BgmSystem& m_bgmSystem;
    UIRenderer& m_uiRenderer;

    GameStateMachine m_stateMachine; // Add StateMachine
    World         m_world;
    physics::PhysicsSystem m_physicsSystem;
    Renderer      m_renderer;
    DropRenderer  m_dropRenderer;
    HumanoidRenderer  m_humanoidRenderer;
    PostProcessRenderer m_postProcessRenderer;
    ParticleSystem m_particleSystem;
    DropSystem m_dropSystem;
    CraftingSystem m_craftingSystem;
    ecs::GameplayScene m_gameplayScene;
    std::string m_lastSubmittedCommand;
#ifndef NDEBUG
    Dashboard      m_dashboard;
#endif


    [[nodiscard]] StateDependencies makeStateDependencies();

    void initWorld();
    void initRenderers();
    void initECS();

    // Camera controller (first/third person)
    CameraController m_cameraController;

#ifndef NDEBUG
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
