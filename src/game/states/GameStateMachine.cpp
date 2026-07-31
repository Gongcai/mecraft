#include "GameStateMachine.h"

void GameStateMachine::pushState(std::unique_ptr<IGameState> state) {
    if (m_dispatching) {
        m_pendingOps.push_back({PendingOpType::Push, std::move(state)});
        return;
    }
    pushStateNow(std::move(state));
}

void GameStateMachine::popState() {
    if (m_dispatching) {
        m_pendingOps.push_back({PendingOpType::Pop, nullptr});
        return;
    }
    popStateNow();
}

void GameStateMachine::changeState(std::unique_ptr<IGameState> state) {
    if (m_dispatching) {
        m_pendingOps.push_back({PendingOpType::Change, std::move(state)});
        return;
    }
    changeStateNow(std::move(state));
}

void GameStateMachine::pushStateNow(std::unique_ptr<IGameState> state) {
    if (state) {
        state->onEnter();
        m_states.push_back(std::move(state));
    }
}

void GameStateMachine::popStateNow() {
    if (!m_states.empty()) {
        m_states.back()->onExit();
        m_states.pop_back();
        // Note: We might want to notify the new top state that it's active again,
        // but for simple stack behavior, this is sufficient.
    }
}

void GameStateMachine::changeStateNow(std::unique_ptr<IGameState> state) {
    while (!m_states.empty()) {
        m_states.back()->onExit();
        m_states.pop_back();
    }
    pushStateNow(std::move(state));
}

void GameStateMachine::applyPendingOps() {
    while (!m_pendingOps.empty()) {
        std::vector<PendingOp> ops;
        ops.swap(m_pendingOps);
        for (PendingOp& op : ops) {
            switch (op.type) {
            case PendingOpType::Push: pushStateNow(std::move(op.state)); break;
            case PendingOpType::Pop: popStateNow(); break;
            case PendingOpType::Change: changeStateNow(std::move(op.state)); break;
            }
        }
    }
}

void GameStateMachine::update(float dt, const InputSnapshot& snapshot) {
    if (!m_states.empty()) {
        m_dispatching = true;
        m_states.back()->update(dt, snapshot);
        m_dispatching = false;
        applyPendingOps();
    }
}

void GameStateMachine::render() {
    if (!m_states.empty()) {
        m_dispatching = true;
        m_states.back()->render();
        m_dispatching = false;
        applyPendingOps();
    }
}

bool GameStateMachine::pausesSimulation() const {
    return !m_states.empty() && m_states.back()->pausesSimulation();
}
