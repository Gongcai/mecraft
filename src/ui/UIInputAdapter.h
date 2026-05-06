#pragma once

#include "UIEventResult.h"
#include "UIInputEvent.h"

class InputContextManager;
struct InputSnapshot;
class UIRenderer;

struct UIInputRouteResult {
    UIEventResult aggregate = UIEventResult::Ignored;
    UIEventResult primaryDown = UIEventResult::Ignored;
    bool primaryPressed = false;
    bool secondaryPressed = false;
    bool primaryReleased = false;
    bool secondaryReleased = false;
};

namespace UIInputAdapter {
    UIInputRouteResult routeInput(UIRenderer& renderer,
                                  const InputSnapshot& snapshot,
                                  const InputContextManager& context);
}
