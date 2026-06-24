#ifndef MECRAFT_SLEEPINGSTATE_H
#define MECRAFT_SLEEPINGSTATE_H

#include "IGameState.h"

class GameStateMachine;
class InputContextManager;
class InputManager;
class World;

class SleepingState final : public IGameState {
public:
    SleepingState(GameStateMachine& fsm,
                  InputContextManager& context,
                  InputManager& input,
                  World& world);

    void onEnter() override;
    void onExit() override;
    void update(float dt, const InputSnapshot& snapshot) override;
    bool pausesSimulation() const override { return true; }

private:
    [[nodiscard]] static bool hasManualWakeInput(const InputSnapshot& snapshot);

    GameStateMachine& m_fsm;
    InputContextManager& m_context;
    InputManager& m_input;
    World& m_world;
    bool m_wakePending = false;
};

#endif // MECRAFT_SLEEPINGSTATE_H
