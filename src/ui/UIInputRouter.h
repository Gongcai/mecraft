#pragma once

#include <vector>

#include "IUIControl.h"

class UIInputRouter {
public:
    void registerControl(IUIControl* control);
    void unregisterControl(const IUIControl* control);
    void clear();

    [[nodiscard]] UIEventResult route(const UIInputEvent& event) const;

private:
    std::vector<IUIControl*> m_controls;
};

