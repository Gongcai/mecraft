#pragma once

#include "UIEventResult.h"
#include "UIInputEvent.h"

class InputContextManager;
struct InputSnapshot;
class UIRenderer;

struct UIInputRouteResult {
    UIEventResult aggregate = UIEventResult::Ignored;
    UIEventResult primaryDown = UIEventResult::Ignored;
};

namespace UIInputAdapter {
    UIInputRouteResult routeInput(UIRenderer& renderer,
                                  const InputSnapshot& snapshot,
                                  const InputContextManager& context);
}
