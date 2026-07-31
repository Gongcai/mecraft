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
    [[nodiscard]] bool pausesSimulation() const;

    /// Pointer to the base gameplay state (bottom of the stack). Always a
    /// GameplayState or CreativeModeState; pushed UI states sit on top of it.
    /// Returns nullptr before the first state is pushed. Not owned - do not delete.
    [[nodiscard]] IGameState* baseState() const { return m_states.empty() ? nullptr : m_states.front().get(); }

    // Quit-to-menu signaling
    void requestQuitToMenu() { m_quitToMenuRequested = true; }
    [[nodiscard]] bool isQuitToMenuRequested() const { return m_quitToMenuRequested; }
    void clearQuitToMenuRequest() { m_quitToMenuRequested = false; }

private:
    enum class PendingOpType { Push, Pop, Change };

    struct PendingOp {
        PendingOpType type = PendingOpType::Pop;
        std::unique_ptr<IGameState> state;
    };

    void pushStateNow(std::unique_ptr<IGameState> state);
    void popStateNow();
    void changeStateNow(std::unique_ptr<IGameState> state);
    void applyPendingOps();

    std::vector<std::unique_ptr<IGameState>> m_states;
    std::vector<PendingOp> m_pendingOps;
    bool m_dispatching = false;
    bool m_quitToMenuRequested = false;
};

#endif //MECRAFT_GAMESTATEMACHINE_H
