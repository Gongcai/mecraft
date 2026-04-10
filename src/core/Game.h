//
// Created by Caiwe on 2026/3/21.
//

#ifndef MECRAFT_GAME_H
#define MECRAFT_GAME_H
#include <iostream>

#include "Camera.h"
#include "InputManager.h"
#include "Window.h"
#include "Time.h"
#include "../player/Player.h"
#ifndef NDEBUG
#include "../renderer/TestCube.h"
#endif
#include "../renderer/Renderer.h"
#include "../renderer/PostProcessRenderer.h"
#include "InputContextManager.h"
#include "../player/ActionMap.h"
#include "GameStateMachine.h"
#include "../world/World.h"
#include "../physics/PhysicsSystem.h"
#ifndef NDEBUG
#include "../ui/Dashboard.h"
#endif
#include "../ui/UIRenderer.h"
#include "../audio/AudioEngine.h"
#include "../particle/ParticleSystem.h"
class Game {
public:
    Game();
    void init(int width, int height, const char* title);
    void run();       // 主循环
    void shutdown();

private:
    static constexpr double TICK_RATE = 1.0 / 60.0;

    Window        m_window;
    InputManager  m_input;
    ActionMap     m_actionMap; // Add ActionMap
    InputContextManager m_contextManager; // Add ContextManager
    GameStateMachine m_stateMachine; // Add StateMachine
    Player        m_player;
    World         m_world;
    physics::PhysicsSystem m_physicsSystem;
    Renderer      m_renderer;
    PostProcessRenderer m_postProcessRenderer;
    ResourceMgr    m_resourceMgr;
    AudioEngine   m_audioEngine;
    ParticleSystem m_particleSystem;
    UIRenderer    m_uiRenderer;
#ifndef NDEBUG
    Dashboard      m_dashboard;
#endif


    [[nodiscard]] static double clampFrameTime(double dt);
    void runFixedUpdate(double fixedStep, double& accumulator);
    void syncAudioListener();
    void renderFrame();

#ifndef NDEBUG
    struct FrameProfilerDebug {
        double fixedUpdateMs = 0.0;
        double audioMs = 0.0;
        double renderMs = 0.0;
        double publishAccumulator = 0.0;
        double publishInterval = 0.25;
    };

    FrameProfilerDebug m_frameProfilerDebug{};
    Dashboard::FrameProfilerStats m_dashboardProfilerStats{};
    void publishDebugFrameProfiler(double frameTime);
#endif
};


#endif //MECRAFT_GAME_H