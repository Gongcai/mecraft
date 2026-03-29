//
// Created by Caiwe on 2026/3/21.
//

#ifndef MECRAFT_GAME_H
#define MECRAFT_GAME_H
#include <iostream>
#include <memory>

#include "Camera.h"
#include "InputManager.h"
#include "Window.h"
#include "Time.h"
#include "../player/Player.h"
#include "../renderer/TestCube.h"
#include "../renderer/Renderer.h"
#include "InputContextManager.h"
#include "../player/ActionMap.h"
#include "GameStateMachine.h"
#include "../world/World.h"
#include "../physics/PhysicsSystem.h"
#include "../ui/Dashboard.h"

class LightEngine;

class Game {
public:
    Game();
    ~Game();  // Add destructor
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
    ResourceMgr    m_resourceMgr;
    Dashboard      m_dashboard;
    std::unique_ptr<LightEngine> m_lightEngine;

    double m_lastFrameTime = 0.0;
    bool m_shutdownCalled = false;
};


#endif //MECRAFT_GAME_H