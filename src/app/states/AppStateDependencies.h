#ifndef MECRAFT_APPSTATEDEPENDENCIES_H
#define MECRAFT_APPSTATEDEPENDENCIES_H

#include <functional>

#include "engine/platform/Window.h"
#include "engine/input/InputManager.h"
#include "../../player/ActionMap.h"
#include "engine/input/InputContextManager.h"
#include "../../resource/GameResources.h"
#include "../../audio/AudioEngine.h"
#include "../../audio/BgmSystem.h"
#include "../../ui/core/UIRenderer.h"
#include "../../locale/LocaleManager.h"

class AppStateMachine;
class RhiDevice;
class RhiCommandListPool;
class ThreadPool;
namespace app::validation {
class ValidationRunController;
}

struct AppStateDependencies {
    AppStateMachine& appFsm;
    Window& window;
    InputManager& input;
    ActionMap& actionMap;
    InputContextManager& contextManager;
    GameResources& resources;
    AudioEngine& audioEngine;
    BgmSystem& bgmSystem;
    UIRenderer& uiRenderer;
    LocaleManager& localeManager;
    ThreadPool& threadPool;
    RhiDevice& rhiDevice;
    RhiCommandListPool& commandListPool;
    app::validation::ValidationRunController& validationRun;
    bool enableDebugDashboard;
    std::function<void()> beginGameplayInputReplay;
    std::function<void()> endGameplayInputReplay;
    std::function<bool()> shouldCloseAppOnGameplayQuitToMenu;
};

#endif //MECRAFT_APPSTATEDEPENDENCIES_H
