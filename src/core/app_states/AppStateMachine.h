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

    [[nodiscard]] bool isEmpty() const { return m_states.empty(); }

private:
    std::vector<std::unique_ptr<IAppState>> m_states;
};

#endif //MECRAFT_APPSTATEMACHINE_H
