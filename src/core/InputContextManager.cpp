#include "InputContextManager.h"

InputContextManager::InputContextManager(ActionMap& actionMap)
    : m_actionMap(actionMap)
{
    // Make sure we start with a context
    m_contextStack.push_back(InputContextType::Gameplay);
}

void InputContextManager::pushContext(InputContextType context) {
    m_contextStack.push_back(context);
}

void InputContextManager::popContext() {
    if (m_contextStack.size() > 1) {
        m_contextStack.pop_back();
    }
}

void InputContextManager::switchContext(InputContextType context) {
    if (!m_contextStack.empty()) {
        m_contextStack.back() = context;
    } else {
        m_contextStack.push_back(context);
    }
}

InputContextType InputContextManager::getCurrentContext() const {
    if (m_contextStack.empty()) return InputContextType::Gameplay; // Safety
    return m_contextStack.back();
}

bool InputContextManager::isContextActive(InputContextType context) const {
    for (const auto& c : m_contextStack) {
        if (c == context) return true;
    }
    return false;
}

bool InputContextManager::isActionTriggered(Action action, const InputSnapshot& snapshot) const {
    // Check Active Context (Top of Stack)
    // Design decision: "从栈顶向下查询动作，命中即停止"
    // We check from top down.
    for (auto it = m_contextStack.rbegin(); it != m_contextStack.rend(); ++it) {
        InputContextType context = *it;
        if (m_actionMap.isActionTriggered(action, context, snapshot)) {
            return true;
        }
        // If we want stop propagation if ANY binding is found in this context (even if not triggered),
        // we would need ActionMap to tell us "binding exists".
        // But for now, simple iteration. If triggered, return true.
        // If two contexts both bind "Jump" to "Space", and both are active, "Jump" triggers.
        // Usually we want higher context to consume input.
        // But if `ActionMap` checks SPECIFIC BINDINGS, we can't easily detect consumption unless we pass the key code.
        // Since we query by Action, we just assume if it triggers, it triggers.
        // Managing consumption is harder with `Action` abstraction because multiple actions might map to same key potentially.
    }
    return false;
}

bool InputContextManager::isActionHeld(Action action, const InputSnapshot& snapshot) const {
    // Forward to isActionTriggered as it handles trigger type checks logic
    // But if caller wants to check "Held" specifically, they should bind action as Held.
    return isActionTriggered(action, snapshot);
}

