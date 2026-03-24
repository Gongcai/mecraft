#ifndef MECRAFT_IGAMESTATE_H
#define MECRAFT_IGAMESTATE_H

#include "InputManager.h"

// Forward declaration
class GameStateMachine;

class IGameState {
public:
    virtual ~IGameState() = default;

    // Called when the state is pushed onto the stack
    virtual void onEnter() {}

    // Called when the state is popped from the stack
    virtual void onExit() {}

    // Called every frame to update state logic
    virtual void update(float dt, const InputSnapshot& snapshot) = 0;

    // Optional: render hook if needed
    virtual void render() {}
};

#endif //MECRAFT_IGAMESTATE_H

