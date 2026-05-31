#ifndef MECRAFT_UI_STATE_CONTEXT_H
#define MECRAFT_UI_STATE_CONTEXT_H

#include "GameStateMachine.h"

class InputContextManager;
class InputManager;
class UIRenderer;
class LocaleManager;

/// Narrow context for UIState — only UI-relevant dependencies.
struct UIStateContext {
    GameStateMachine& fsm;
    InputContextManager& context;
    InputManager& input;
    UIRenderer& uiRenderer;
    const LocaleManager& localeManager;
};

#endif // MECRAFT_UI_STATE_CONTEXT_H
