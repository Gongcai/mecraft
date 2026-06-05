#ifndef MECRAFT_APPSTATEMACHINE_H
#define MECRAFT_APPSTATEMACHINE_H

#include <vector>
#include <memory>
#include "IAppState.h"

class AppStateMachine {
public:
    AppStateMachine() = default;

    void pushState(std::unique_ptr<IAppState> state);
    void popState();
    void changeState(std::unique_ptr<IAppState> state);

    void update(double frameTime, double& accumulator);
    void render(double frameTime);

#ifdef MECRAFT_DEBUG
    void recordPollEvents(double ms,
                          unsigned keyEvents,
                          unsigned mouseButtonEvents,
                          unsigned cursorPosEvents,
                          unsigned scrollEvents,
                          unsigned charEvents);
    void recordAppUpdateDispatch(double ms);
    void recordAppRenderDispatch(double ms);
#endif

    [[nodiscard]] bool isEmpty() const { return m_states.empty(); }

private:
    enum class PendingOpType {
        Push,
        Pop,
        Change
    };

    struct PendingOp {
        PendingOpType type = PendingOpType::Pop;
        std::unique_ptr<IAppState> state;
    };

    void pushStateNow(std::unique_ptr<IAppState> state);
    void popStateNow();
    void changeStateNow(std::unique_ptr<IAppState> state);
    void applyPendingOps();

    std::vector<std::unique_ptr<IAppState>> m_states;
    std::vector<PendingOp> m_pendingOps;
    bool m_dispatching = false;
};

#endif //MECRAFT_APPSTATEMACHINE_H
