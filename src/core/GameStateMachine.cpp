#include "GameStateMachine.h"

void GameStateMachine::pushState(std::unique_ptr<IGameState> state) {
    if (state) {
        state->onEnter();
        m_states.push_back(std::move(state));
    }
}

void GameStateMachine::popState() {
    if (!m_states.empty()) {
        m_states.back()->onExit();
        m_states.pop_back();
        // Note: We might want to notify the new top state that it's active again,
        // but for simple stack behavior, this is sufficient.
    }
}

void GameStateMachine::changeState(std::unique_ptr<IGameState> state) {
    while (!m_states.empty()) {
        m_states.back()->onExit();
        m_states.pop_back();
    }
    pushState(std::move(state));
}

void GameStateMachine::update(float dt, const InputSnapshot& snapshot) {
    if (!m_states.empty()) {
        m_states.back()->update(dt, snapshot);
    }
}

void GameStateMachine::render() {
    if (!m_states.empty()) {
        m_states.back()->render();
    }
}

