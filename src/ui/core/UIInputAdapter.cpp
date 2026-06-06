#include "UIInputAdapter.h"

#include <cstddef>
#include <cstdint>

#include "engine/input/InputContextManager.h"
#include "engine/input/InputManager.h"
#include "UIRenderer.h"

namespace {

int currentModifiers(const InputSnapshot& snapshot) {
    int modifiers = 0;
    if (snapshot.isKeyHeld(GLFW_KEY_LEFT_SHIFT) || snapshot.isKeyHeld(GLFW_KEY_RIGHT_SHIFT)) {
        modifiers |= uiInputModifierMask(UIInputModifier::Shift);
    }
    if (snapshot.isKeyHeld(GLFW_KEY_LEFT_CONTROL) || snapshot.isKeyHeld(GLFW_KEY_RIGHT_CONTROL)) {
        modifiers |= uiInputModifierMask(UIInputModifier::Control);
    }
    if (snapshot.isKeyHeld(GLFW_KEY_LEFT_ALT) || snapshot.isKeyHeld(GLFW_KEY_RIGHT_ALT)) {
        modifiers |= uiInputModifierMask(UIInputModifier::Alt);
    }
    if (snapshot.isKeyHeld(GLFW_KEY_LEFT_SUPER) || snapshot.isKeyHeld(GLFW_KEY_RIGHT_SUPER)) {
        modifiers |= uiInputModifierMask(UIInputModifier::Super);
    }
    return modifiers;
}

void mergeResult(UIEventResult& aggregate, const UIEventResult result) {
    if (result == UIEventResult::Consumed) {
        aggregate = UIEventResult::Consumed;
    } else if (result == UIEventResult::Handled && aggregate == UIEventResult::Ignored) {
        aggregate = UIEventResult::Handled;
    }
}

UIInputEvent makePointerEvent(const UIInputEventType type,
                              const InputSnapshot& snapshot,
                              const UIPointerButton button) {
    UIInputEvent event;
    event.type = type;
    event.x = snapshot.mousePosition.x;
    event.y = snapshot.mousePosition.y;
    event.button = button;
    event.modifiers = currentModifiers(snapshot);
    return event;
}

UIInputEvent makeCommandEvent(const InputSnapshot& snapshot, const UICommand command) {
    UIInputEvent event;
    event.type = UIInputEventType::Command;
    event.x = snapshot.mousePosition.x;
    event.y = snapshot.mousePosition.y;
    event.modifiers = currentModifiers(snapshot);
    event.command = command;
    return event;
}

UIInputEvent makeScrollEvent(const InputSnapshot& snapshot) {
    UIInputEvent event;
    event.type = UIInputEventType::Scroll;
    event.x = snapshot.mousePosition.x;
    event.y = snapshot.mousePosition.y;
    event.modifiers = currentModifiers(snapshot);
    event.scrollY = static_cast<float>(snapshot.scrollDelta);
    return event;
}

UIInputEvent makeTextInputEvent(const InputSnapshot& snapshot, const std::uint32_t codepoint) {
    UIInputEvent event;
    event.type = UIInputEventType::TextInput;
    event.x = snapshot.mousePosition.x;
    event.y = snapshot.mousePosition.y;
    event.modifiers = currentModifiers(snapshot);
    event.codepoint = codepoint;
    return event;
}

UIInputEvent makeKeyEvent(const InputSnapshot& snapshot,
                          const UIInputEventType type,
                          const int key) {
    UIInputEvent event;
    event.type = type;
    event.x = snapshot.mousePosition.x;
    event.y = snapshot.mousePosition.y;
    event.key = key;
    event.modifiers = currentModifiers(snapshot);
    return event;
}

void routeCommand(UIRenderer& renderer,
                  const InputSnapshot& snapshot,
                  UIInputRouteResult& routeResult,
                  const UICommand command) {
    const UIEventResult result = renderer.routeUIInput(makeCommandEvent(snapshot, command));
    mergeResult(routeResult.aggregate, result);
}

} // namespace

UIInputRouteResult UIInputAdapter::routeInput(UIRenderer& renderer,
                                              const InputSnapshot& snapshot,
                                              const InputContextManager& context) {
    UIInputRouteResult routeResult;

    const int primaryButton = static_cast<int>(UIPointerButton::Primary);
    const int secondaryButton = static_cast<int>(UIPointerButton::Secondary);

    routeResult.primaryPressed = snapshot.isMouseButtonJustPressed(primaryButton);
    routeResult.secondaryPressed = snapshot.isMouseButtonJustPressed(secondaryButton);
    routeResult.primaryReleased = snapshot.isMouseButtonJustReleased(primaryButton);
    routeResult.secondaryReleased = snapshot.isMouseButtonJustReleased(secondaryButton);

    const bool hasButtonEvent = routeResult.primaryPressed ||
                                routeResult.secondaryPressed ||
                                routeResult.primaryReleased ||
                                routeResult.secondaryReleased;

    // Only send PointerMove when mouse actually moved or a button event needs
    // up-to-date hover state. Skips redundant hit-testing on stationary frames.
    static float lastX = -1.0f;
    static float lastY = -1.0f;
    const float curX = snapshot.mousePosition.x;
    const float curY = snapshot.mousePosition.y;
    const bool pointerMoved = (curX != lastX || curY != lastY);
    lastX = curX;
    lastY = curY;

    if (pointerMoved || hasButtonEvent) {
        mergeResult(routeResult.aggregate,
                    renderer.routeUIInput(makePointerEvent(UIInputEventType::PointerMove,
                                                           snapshot,
                                                           UIPointerButton::None)));
    }

    if (snapshot.isMouseButtonJustPressed(primaryButton)) {
        routeResult.primaryDown = renderer.routeUIInput(makePointerEvent(UIInputEventType::PointerDown,
                                                                         snapshot,
                                                                         UIPointerButton::Primary));
        mergeResult(routeResult.aggregate, routeResult.primaryDown);
    }
    if (snapshot.isMouseButtonJustPressed(secondaryButton)) {
        mergeResult(routeResult.aggregate,
                    renderer.routeUIInput(makePointerEvent(UIInputEventType::PointerDown,
                                                           snapshot,
                                                           UIPointerButton::Secondary)));
    }
    if (snapshot.isMouseButtonJustReleased(primaryButton)) {
        mergeResult(routeResult.aggregate,
                    renderer.routeUIInput(makePointerEvent(UIInputEventType::PointerUp,
                                                           snapshot,
                                                           UIPointerButton::Primary)));
    }
    if (snapshot.isMouseButtonJustReleased(secondaryButton)) {
        mergeResult(routeResult.aggregate,
                    renderer.routeUIInput(makePointerEvent(UIInputEventType::PointerUp,
                                                           snapshot,
                                                           UIPointerButton::Secondary)));
    }

    if (snapshot.scrollDelta != 0.0) {
        mergeResult(routeResult.aggregate, renderer.routeUIInput(makeScrollEvent(snapshot)));
    }

    for (int key = 0; key <= GLFW_KEY_LAST; ++key) {
        if (snapshot.isKeyJustPressed(key)) {
            mergeResult(routeResult.aggregate,
                        renderer.routeUIInput(makeKeyEvent(snapshot, UIInputEventType::KeyDown, key)));
        }
        if (snapshot.isKeyJustReleased(key)) {
            mergeResult(routeResult.aggregate,
                        renderer.routeUIInput(makeKeyEvent(snapshot, UIInputEventType::KeyUp, key)));
        }
    }
    if (routeResult.aggregate == UIEventResult::Consumed) {
        return routeResult;
    }

    for (std::size_t i = 0; i < snapshot.typedCharCount; ++i) {
        mergeResult(routeResult.aggregate,
                    renderer.routeUIInput(makeTextInputEvent(snapshot, snapshot.typedChars[i])));
    }
    if (routeResult.aggregate == UIEventResult::Consumed) {
        return routeResult;
    }

    if (context.isActionTriggered(Action::Up)) {
        routeCommand(renderer, snapshot, routeResult, UICommand::NavigateUp);
    }
    if (context.isActionTriggered(Action::Down)) {
        routeCommand(renderer, snapshot, routeResult, UICommand::NavigateDown);
    }
    if (context.isActionTriggered(Action::Left)) {
        routeCommand(renderer, snapshot, routeResult, UICommand::NavigateLeft);
    }
    if (context.isActionTriggered(Action::Right)) {
        routeCommand(renderer, snapshot, routeResult, UICommand::NavigateRight);
    }
    if (context.isActionTriggered(Action::Confirm)) {
        routeCommand(renderer, snapshot, routeResult, UICommand::Activate);
    }
    if (context.isActionTriggered(Action::Cancel)) {
        routeCommand(renderer, snapshot, routeResult, UICommand::Cancel);
    }

    return routeResult;
}
