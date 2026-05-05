#include <cstdlib>
#include <iostream>
#include <memory>

#include <GLFW/glfw3.h>

#include "../src/ui/UIButton.h"
#include "../src/ui/UIScene.h"

namespace {

int fail(const char* message) {
    std::cerr << "[ui_focus_navigation_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

class TestScene final : public UIScene {};

UIInputEvent keyDown(float x, float y, int key) {
    return {UIInputEventType::KeyDown, x, y, UIPointerButton::None, key};
}

UIInputEvent keyUp(float x, float y, int key) {
    return {UIInputEventType::KeyUp, x, y, UIPointerButton::None, key};
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

    if (scene.onInput(keyDown(0.0f, 0.0f, GLFW_KEY_DOWN)) != UIEventResult::Handled) {
        return fail("down key should move focus and be handled");
    }
    if (!secondPtr->isFocused()) {
        return fail("down key should move focus to next widget");
    }

    if (scene.onInput(keyDown(0.0f, 0.0f, GLFW_KEY_ENTER)) != UIEventResult::Handled) {
        return fail("enter keydown should be handled by focused button");
    }
    if (scene.onInput(keyUp(0.0f, 0.0f, GLFW_KEY_ENTER)) != UIEventResult::Consumed) {
        return fail("enter keyup should click focused button");
    }
    if (secondClicks != 1 || firstClicks != 0) {
        return fail("enter should click currently focused widget only");
    }

    // Pointer coordinates are top-left origin. This lands inside the first button.
    const UIEventResult pointerDown = scene.onInput({UIInputEventType::PointerDown, 20.0f, 80.0f, UIPointerButton::Primary});
    if (pointerDown != UIEventResult::Handled) {
        return fail("pointer down inside button should be handled");
    }
    if (!firstPtr->isFocused()) {
        return fail("pointer interaction should request and acquire focus");
    }

    if (scene.onInput(keyDown(0.0f, 0.0f, GLFW_KEY_ENTER)) != UIEventResult::Handled) {
        return fail("enter keydown should be handled by newly focused widget");
    }
    if (scene.onInput(keyUp(0.0f, 0.0f, GLFW_KEY_ENTER)) != UIEventResult::Consumed) {
        return fail("enter keyup should click newly focused widget");
    }
    if (firstClicks != 1) {
        return fail("focused widget should receive enter click after focus change");
    }

    std::cout << "[ui_focus_navigation_test] PASS\n";
    return EXIT_SUCCESS;
}
