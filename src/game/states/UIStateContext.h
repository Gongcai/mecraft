#ifndef MECRAFT_UI_STATE_CONTEXT_H
#define MECRAFT_UI_STATE_CONTEXT_H

#include "GameStateMachine.h"

class InputContextManager;
class InputManager;
class UIRenderer;
class LocaleManager;
class RenderScene;
class World;

/// Narrow context for UIState — only UI-relevant dependencies.
struct UIStateContext {
    GameStateMachine& fsm;
    InputContextManager& context;
    InputManager& input;
    UIRenderer& uiRenderer;
    const LocaleManager& localeManager;
    RenderScene* renderScene = nullptr;  ///< Optional; for settings screen
    World* world = nullptr;              ///< Optional; for render distance control
};

#endif // MECRAFT_UI_STATE_CONTEXT_H
