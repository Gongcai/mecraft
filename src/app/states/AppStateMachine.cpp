#include "AppStateMachine.h"

void AppStateMachine::pushState(std::unique_ptr<IAppState> state) {
    if (m_dispatching) {
        m_pendingOps.push_back({PendingOpType::Push, std::move(state)});
        return;
    }
    pushStateNow(std::move(state));
}

void AppStateMachine::popState() {
    if (m_dispatching) {
        m_pendingOps.push_back({PendingOpType::Pop, nullptr});
        return;
    }
    popStateNow();
}

void AppStateMachine::changeState(std::unique_ptr<IAppState> state) {
    if (m_dispatching) {
        m_pendingOps.push_back({PendingOpType::Change, std::move(state)});
        return;
    }
    changeStateNow(std::move(state));
}

void AppStateMachine::pushStateNow(std::unique_ptr<IAppState> state) {
    if (!state) {
        return;
    }
    m_states.push_back(std::move(state));
    m_states.back()->onEnter();
}

void AppStateMachine::popStateNow() {
    if (!m_states.empty()) {
        m_states.back()->onExit();
        m_states.pop_back();
    }
}

void AppStateMachine::changeStateNow(std::unique_ptr<IAppState> state) {
    while (!m_states.empty()) {
        m_states.back()->onExit();
        m_states.pop_back();
    }
    pushStateNow(std::move(state));
}

void AppStateMachine::applyPendingOps() {
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

void AppStateMachine::update(double frameTime, double& accumulator) {
    if (!m_states.empty()) {
        m_dispatching = true;
        m_states.back()->update(frameTime, accumulator);
        m_dispatching = false;
        applyPendingOps();
    }
}

void AppStateMachine::render(double frameTime) {
    if (!m_states.empty()) {
        m_dispatching = true;
        m_states.back()->render(frameTime);
        m_dispatching = false;
        applyPendingOps();
    }
}

const GpuFrameStats* AppStateMachine::gpuFrameStats() const {
    if (m_states.empty()) {
        return nullptr;
    }
    return m_states.back()->gpuFrameStats();
}

RenderGraphFrameStats AppStateMachine::renderGraphFrameStats() const {
    if (m_states.empty()) {
        return {};
    }
    return m_states.back()->renderGraphFrameStats();
}

#ifdef MECRAFT_DEBUG
void AppStateMachine::recordPollEvents(double ms, unsigned keyEvents, unsigned mouseButtonEvents,
                                       unsigned cursorPosEvents, unsigned scrollEvents, unsigned charEvents,
                                       double inputCallbackMs, double cursorPosCallbackMs, double imguiCallbackMs,
                                       double imguiCursorPosCallbackMs, double imguiCursorPosBackendMs,
                                       double imguiWndProcMs, double imguiWndProcSlowestMs,
                                       unsigned imguiWndProcSlowestMsg, unsigned imguiWndProcCount) {
    if (!m_states.empty()) {
        m_states.back()->recordPollEvents(ms, keyEvents, mouseButtonEvents, cursorPosEvents, scrollEvents, charEvents,
                                          inputCallbackMs, cursorPosCallbackMs, imguiCallbackMs,
                                          imguiCursorPosCallbackMs, imguiCursorPosBackendMs, imguiWndProcMs,
                                          imguiWndProcSlowestMs, imguiWndProcSlowestMsg, imguiWndProcCount);
    }
}

void AppStateMachine::recordAppUpdateDispatch(double ms) {
    if (!m_states.empty()) {
        m_states.back()->recordAppUpdateDispatch(ms);
    }
}

void AppStateMachine::recordAppRenderDispatch(double ms) {
    if (!m_states.empty()) {
        m_states.back()->recordAppRenderDispatch(ms);
    }
}
#endif
