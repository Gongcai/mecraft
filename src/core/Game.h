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
#include "../renderer/TestCube.h"

class Game {
public:
    void init(int width, int height, const char* title);
    void run();       // 主循环
    void shutdown();

private:
    static constexpr double TICK_RATE = 1.0 / 60.0;

    Window        m_window;
    InputManager  m_input;
    Camera        m_camera;
    TestCube    *m_testCube = nullptr;


    double m_lastFrameTime = 0.0;
};


#endif //MECRAFT_GAME_H