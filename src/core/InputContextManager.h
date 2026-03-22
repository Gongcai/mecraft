#ifndef MECRAFT_INPUTCONTEXTMANAGER_H
#define MECRAFT_INPUTCONTEXTMANAGER_H

#include <vector>
#include <memory>
#include "../player/ActionMap.h"

// InputContextType enum is defined in ActionMap.h

class InputContextManager {
public:
    InputContextManager(ActionMap& actionMap);

    // Context Stack Management
    void pushContext(InputContextType context);
    void popContext();
    void switchContext(InputContextType context);
    [[nodiscard]] InputContextType getCurrentContext() const;
    [[nodiscard]] bool isContextActive(InputContextType context) const;

    // Action Query Interface
    // Called by Player/UI to check if an action effectively triggered this frame
    // Considering active context stack
    [[nodiscard]] bool isActionTriggered(Action action, const InputSnapshot& snapshot) const;
    [[nodiscard]] bool isActionHeld(Action action, const InputSnapshot& snapshot) const;

private:
    std::vector<InputContextType> m_contextStack;
    ActionMap& m_actionMap; // Reference to the action map data
};

#endif //MECRAFT_INPUTCONTEXTMANAGER_H


