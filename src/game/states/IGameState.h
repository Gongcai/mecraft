#ifndef MECRAFT_IGAMESTATE_H
#define MECRAFT_IGAMESTATE_H

#include "engine/input/InputManager.h"
#include "../modes/GameplayModeRules.h"

// Forward declaration
class GameStateMachine;

enum class GameStateKind {
    Generic,
    Gameplay,
    DataDrivenContainer,
    SmeltingContainer,
    Workbench,
    NetworkContainer
};

class IGameState {
public:
    virtual ~IGameState() = default;

    // Called when the state is pushed onto the stack
    virtual void onEnter() {}

    // Called when the state is popped from the stack
    virtual void onExit() {}

    // Called every frame to update state logic
    virtual void update(float dt, const InputSnapshot& snapshot) = 0;

    // Whether this state should stop gameplay simulation while it is active.
    virtual bool pausesSimulation() const { return false; }

    // Explicit state category used where RTTI-based downcasts would otherwise be needed.
    [[nodiscard]] virtual GameStateKind kind() const { return GameStateKind::Generic; }

    // Gameplay states override this to expose the authoritative mode without RTTI.
    [[nodiscard]] virtual GameplayMode gameplayMode() const { return GameplayMode::Survival; }

    // Optional: render hook if needed
    virtual void render() {}
};

#endif //MECRAFT_IGAMESTATE_H
