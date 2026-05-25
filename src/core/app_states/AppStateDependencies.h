#ifndef MECRAFT_APPSTATEDEPENDENCIES_H
#define MECRAFT_APPSTATEDEPENDENCIES_H

#include "../Window.h"
#include "../InputManager.h"
#include "../../player/ActionMap.h"
#include "../InputContextManager.h"
#include "../../resource/ResourceMgr.h"
#include "../../audio/AudioEngine.h"
#include "../../audio/BgmSystem.h"
#include "../../ui/core/UIRenderer.h"
#include "../../locale/LocaleManager.h"

class AppStateMachine;

struct AppStateDependencies {
    AppStateMachine& appFsm;
    Window& window;
    InputManager& input;
    ActionMap& actionMap;
    InputContextManager& contextManager;
    ResourceMgr& resourceMgr;
    AudioEngine& audioEngine;
    BgmSystem& bgmSystem;
    UIRenderer& uiRenderer;
    LocaleManager& localeManager;
};

#endif //MECRAFT_APPSTATEDEPENDENCIES_H
