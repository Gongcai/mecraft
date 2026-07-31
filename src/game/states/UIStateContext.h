#ifndef MECRAFT_UI_STATE_CONTEXT_H
#define MECRAFT_UI_STATE_CONTEXT_H

#include "GameStateMachine.h"

#include <functional>

class InputContextManager;
class InputManager;
class UIRenderer;
class LocaleManager;
class RenderScene;
class World;
class Window;
class PresentationController;

/// Narrow context for UIState — only UI-relevant dependencies.
struct UIStateContext {
    GameStateMachine& fsm;
    InputContextManager& context;
    InputManager& input;
    UIRenderer& uiRenderer;
    const LocaleManager& localeManager;
    RenderScene* renderScene = nullptr; ///< Optional; for settings screen
    World* world = nullptr; ///< Optional; for render distance control
    std::function<void(int)> renderDistanceSetter;
    Window* window = nullptr;
    PresentationController* presentationController = nullptr;
};

#endif // MECRAFT_UI_STATE_CONTEXT_H
