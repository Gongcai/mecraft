#include "AppStateMachine.h"

void AppStateMachine::pushState(std::unique_ptr<IAppState> state) {
    m_states.push_back(std::move(state));
    m_states.back()->onEnter();
}

void AppStateMachine::popState() {
    if (!m_states.empty()) {
        m_states.back()->onExit();
        m_states.pop_back();
    }
}

void AppStateMachine::changeState(std::unique_ptr<IAppState> state) {
    while (!m_states.empty()) {
        m_states.back()->onExit();
        m_states.pop_back();
    }
    m_states.push_back(std::move(state));
    m_states.back()->onEnter();
}

void AppStateMachine::update(double frameTime, double& accumulator) {
    if (!m_states.empty()) {
        m_states.back()->update(frameTime, accumulator);
    }
}

void AppStateMachine::render(double frameTime) {
    if (!m_states.empty()) {
        m_states.back()->render(frameTime);
    }
}
