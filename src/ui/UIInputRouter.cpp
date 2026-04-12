#include "UIInputRouter.h"

#include <algorithm>

void UIInputRouter::registerControl(IUIControl* control)
{
    if (!control) {
        return;
    }
    if (std::find(m_controls.begin(), m_controls.end(), control) != m_controls.end()) {
        return;
    }
    m_controls.push_back(control);
}

void UIInputRouter::unregisterControl(const IUIControl* control)
{
    if (!control) {
        return;
    }
    m_controls.erase(
        std::remove_if(m_controls.begin(), m_controls.end(),
                       [control](const IUIControl* entry) { return entry == control; }),
        m_controls.end());
}

void UIInputRouter::clear()
{
    m_controls.clear();
}

UIEventResult UIInputRouter::route(const UIInputEvent& event) const
{
    UIEventResult aggregate = UIEventResult::Ignored;

    for (auto it = m_controls.rbegin(); it != m_controls.rend(); ++it) {
        IUIControl* control = *it;
        if (!control || !control->isVisible()) {
            continue;
        }

        const UIEventResult result = control->onInput(event);
        if (result == UIEventResult::Consumed) {
            return UIEventResult::Consumed;
        }
        if (result == UIEventResult::Handled) {
            aggregate = UIEventResult::Handled;
        }
    }

    return aggregate;
}

