#ifndef MECRAFT_GAMEMANAGER_H
#define MECRAFT_GAMEMANAGER_H

#include <memory>
#include <string>
#include <array>

#include "../engine/platform/Window.h"
#include "../engine/input/InputManager.h"
#include "../player/ActionMap.h"
#include "../engine/input/InputContextManager.h"
#include "../resource/ResourceMgr.h"
#include "../audio/AudioEngine.h"
#include "../audio/BgmSystem.h"
#include "../ui/core/UIRenderer.h"
#include "states/AppStateMachine.h"
#include "states/AppStateDependencies.h"
#include "../locale/LocaleManager.h"

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
    LocaleManager m_localeManager;

    AppStateMachine m_appStateMachine;
};

#endif //MECRAFT_GAMEMANAGER_H
