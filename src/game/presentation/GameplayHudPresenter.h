#ifndef MECRAFT_GAMEPLAY_HUD_PRESENTER_H
#define MECRAFT_GAMEPLAY_HUD_PRESENTER_H

#include "GameplayPresentationSnapshot.h"

class Window;
class UIRenderer;
class GameStateMachine;
class InputManager;
struct InputSnapshot;

/// Renders gameplay HUD, state machine overlays, and debug dashboard.
/// Extracted from Game::renderUI() to decouple UI presentation from Game.
class GameplayHudPresenter {
public:
    GameplayHudPresenter(Window& window, UIRenderer& uiRenderer, InputManager& input)
        : m_window(window), m_uiRenderer(uiRenderer), m_input(input) {}

    /// Render gameplay UI overlay.
    /// @param snap The presentation snapshot (player stats, held item motion)
    /// @param stateMachine The game state machine (for pause menu, inventory, etc.)
    void render(const GameplayPresentationSnapshot& snap, GameStateMachine& stateMachine);

private:
    Window& m_window;
    UIRenderer& m_uiRenderer;
    InputManager& m_input;
};

#endif // MECRAFT_GAMEPLAY_HUD_PRESENTER_H
