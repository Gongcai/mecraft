#ifndef MECRAFT_GAMEMANAGER_H
#define MECRAFT_GAMEMANAGER_H

#include <memory>
#include <string>
#include <array>

#include "Window.h"
#include "InputManager.h"
#include "../player/ActionMap.h"
#include "InputContextManager.h"
#include "../resource/ResourceMgr.h"
#include "../audio/AudioEngine.h"
#include "../audio/BgmSystem.h"
#include "../ui/UIRenderer.h"
#include "app_states/AppStateMachine.h"
#include "app_states/AppStateDependencies.h"

class GameManager {
public:
    GameManager();
    ~GameManager();

    void init(int width, int height, const char* title);
    void run();
    void shutdown();

private:
    bool initWindow(int width, int height, const char* title);
    void initResources();
    
    [[nodiscard]] AppStateDependencies makeAppStateDependencies();

    [[nodiscard]] static double clampFrameTime(double dt);

    Window m_window;
    InputManager m_input;
    ActionMap m_actionMap;
    InputContextManager m_contextManager;
    ResourceMgr m_resourceMgr;
    AudioEngine m_audioEngine;
    BgmSystem m_bgmSystem;
    UIRenderer m_uiRenderer;

    AppStateMachine m_appStateMachine;
};

#endif //MECRAFT_GAMEMANAGER_H
