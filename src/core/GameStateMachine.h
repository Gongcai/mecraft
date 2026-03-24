#ifndef MECRAFT_GAMESTATEMACHINE_H
#define MECRAFT_GAMESTATEMACHINE_H

#include <vector>
#include <memory>
#include "IGameState.h"

class GameStateMachine {
public:
    GameStateMachine() = default;

    // Push a new state onto the stack (e.g. open UI)
    void pushState(std::unique_ptr<IGameState> state);

    // Pop the current state from the stack (e.g. close UI)
    void popState();

    // Replace the entire stack with a new state (e.g. switch level/scene)
    void changeState(std::unique_ptr<IGameState> state);

    // Update the active state
    void update(float dt, const InputSnapshot& snapshot);

    // Render the active state (optional)
    void render();

    [[nodiscard]] bool isEmpty() const { return m_states.empty(); }

private:
    std::vector<std::unique_ptr<IGameState>> m_states;
};

#endif //MECRAFT_GAMESTATEMACHINE_H

