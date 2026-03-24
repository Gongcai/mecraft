#ifndef MECRAFT_INPUTCONTEXTMANAGER_H
#define MECRAFT_INPUTCONTEXTMANAGER_H

#include <vector>
#include <memory>
#include "../player/ActionMap.h"

// InputContextType enum is defined in ActionMap.h

class InputContextManager {
public:
    InputContextManager(ActionMap& actionMap,const InputManager& inputManager);

    // Context Stack Management
    void pushContext(InputContextType context);
    void popContext();
    void switchContext(InputContextType context);
    [[nodiscard]] InputContextType getCurrentContext() const;
    [[nodiscard]] bool isContextActive(InputContextType context) const;

    // Action Query Interface
    // Called by Player/UI to check if an action effectively triggered this frame
    // Considering active context stack
    [[nodiscard]] bool isActionTriggered(Action action) const;
    [[nodiscard]] bool isActionHeld(Action action) const;

    [[nodiscard]] float getAxisValue(Axis axis) const;

private:
    std::vector<InputContextType> m_contextStack;
    const InputManager& m_inputManager;
    ActionMap& m_actionMap; // Reference to the action map data
};

#endif //MECRAFT_INPUTCONTEXTMANAGER_H


