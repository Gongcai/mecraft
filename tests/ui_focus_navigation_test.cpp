#include <cstdlib>
#include <iostream>
#include <memory>

#include "../src/ui/UIButton.h"
#include "../src/ui/UIScene.h"

namespace {

int fail(const char* message) {
    std::cerr << "[ui_focus_navigation_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

class TestScene final : public UIScene {};

UIInputEvent command(float x, float y, UICommand command) {
    UIInputEvent event;
    event.type = UIInputEventType::Command;
    event.x = x;
    event.y = y;
    event.command = command;
    return event;
}

} // namespace

int main() {
    TestScene scene;

    int firstClicks = 0;
    int secondClicks = 0;

    auto first = std::make_unique<UIButton>();
    first->anchor = Anchor::BottomLeft;
    first->x = 10.0f;
    first->y = 10.0f;
    first->width = 80.0f;
    first->height = 20.0f;
    first->setOnClick([&firstClicks]() { ++firstClicks; });
    UIButton* firstPtr = first.get();

    auto second = std::make_unique<UIButton>();
    second->anchor = Anchor::BottomLeft;
    second->x = 10.0f;
    second->y = 40.0f;
    second->width = 80.0f;
    second->height = 20.0f;
    second->setOnClick([&secondClicks]() { ++secondClicks; });
    UIButton* secondPtr = second.get();

    scene.addRoot(std::move(first));
    scene.addRoot(std::move(second));

    UIRenderContext context;
    context.screenWidth = 120;
    context.screenHeight = 100;
    scene.setInputContext(context);
    scene.enterScene();

    if (!firstPtr->isFocused()) {
        return fail("first focusable widget should receive initial focus");
    }

    if (scene.onInput(command(0.0f, 0.0f, UICommand::NavigateDown)) != UIEventResult::Handled) {
        return fail("navigate down should move focus and be handled");
    }
    if (!secondPtr->isFocused()) {
        return fail("navigate down should move focus to next widget");
    }

    if (scene.onInput(command(0.0f, 0.0f, UICommand::Activate)) != UIEventResult::Consumed) {
        return fail("activate should click focused button");
    }
    if (secondClicks != 1 || firstClicks != 0) {
        return fail("activate should click currently focused widget only");
    }

    // Pointer coordinates are top-left origin. This lands inside the first button.
    const UIEventResult pointerDown = scene.onInput({UIInputEventType::PointerDown, 20.0f, 80.0f, UIPointerButton::Primary});
    if (pointerDown != UIEventResult::Handled) {
        return fail("pointer down inside button should be handled");
    }
    if (!firstPtr->isFocused()) {
        return fail("pointer interaction should request and acquire focus");
    }

    if (scene.onInput(command(0.0f, 0.0f, UICommand::Activate)) != UIEventResult::Consumed) {
        return fail("activate should click newly focused widget");
    }
    if (firstClicks != 1) {
        return fail("focused widget should receive enter click after focus change");
    }

    std::cout << "[ui_focus_navigation_test] PASS\n";
    return EXIT_SUCCESS;
}
