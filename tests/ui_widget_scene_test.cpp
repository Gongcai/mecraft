#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>

#include "../src/ui/UIScene.h"

namespace {

int fail(const char* message) {
    std::cerr << "[ui_widget_scene_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

bool almostEqual(float a, float b) {
    return std::fabs(a - b) < 0.001f;
}

class FixedResultWidget final : public UIWidget {
public:
    explicit FixedResultWidget(UIEventResult result)
        : m_result(result) {}

    UIEventResult onInput(const UIInputEvent&, const UIRenderContext&) override {
        ++m_callCount;
        return m_result;
    }

    [[nodiscard]] int callCount() const { return m_callCount; }

private:
    UIEventResult m_result = UIEventResult::Ignored;
    int m_callCount = 0;
};

class ContextAwareWidget final : public UIWidget {
public:
    UIEventResult onInput(const UIInputEvent&, const UIRenderContext& ctx) override {
        ++m_callCount;
        return (ctx.screenWidth > 0 && ctx.screenHeight > 0) ? UIEventResult::Handled : UIEventResult::Ignored;
    }

    [[nodiscard]] int callCount() const { return m_callCount; }

private:
    int m_callCount = 0;
};

class TestScene final : public UIScene {};

} // namespace

int main() {
    UIRenderContext context;
    context.screenWidth = 300;
    context.screenHeight = 200;

    UIWidget parent;
    parent.anchor = Anchor::BottomLeft;
    parent.width = 100.0f;
    parent.height = 50.0f;
    parent.x = 10.0f;
    parent.y = 20.0f;
    parent.anchorOffsetX = 2.0f;
    parent.anchorOffsetY = 3.0f;

    if (!almostEqual(parent.getAbsoluteX(context), 12.0f) || !almostEqual(parent.getAbsoluteY(context), 23.0f)) {
        return fail("widget absolute position should include x/y offsets");
    }

    auto child = std::make_unique<FixedResultWidget>(UIEventResult::Ignored);
    child->anchor = Anchor::BottomLeft;
    child->width = 20.0f;
    child->height = 10.0f;
    child->x = 4.0f;
    child->y = 5.0f;
    child->anchorOffsetX = 1.0f;
    child->anchorOffsetY = 2.0f;
    FixedResultWidget* childPtr = child.get();
    parent.addChild(std::move(child));

    if (!almostEqual(childPtr->getAbsoluteX(context), 17.0f) || !almostEqual(childPtr->getAbsoluteY(context), 30.0f)) {
        return fail("child absolute position should include parent and local x/y offsets");
    }

    UIWidget eventRoot;
    auto handledChild = std::make_unique<FixedResultWidget>(UIEventResult::Handled);
    auto ignoredChild = std::make_unique<FixedResultWidget>(UIEventResult::Ignored);
    FixedResultWidget* handledPtr = handledChild.get();
    FixedResultWidget* ignoredPtr = ignoredChild.get();
    eventRoot.addChild(std::move(handledChild));
    eventRoot.addChild(std::move(ignoredChild));
    if (eventRoot.onInput({UIInputEventType::PointerMove, 0.0f, 0.0f, UIPointerButton::None}, context) != UIEventResult::Handled) {
        return fail("UIWidget should propagate Handled from children");
    }
    if (handledPtr->callCount() != 1 || ignoredPtr->callCount() != 1) {
        return fail("UIWidget should still visit children in reverse order for non-consumed results");
    }

    UIWidget consumedRoot;
    auto lowerHandled = std::make_unique<FixedResultWidget>(UIEventResult::Handled);
    auto topConsumed = std::make_unique<FixedResultWidget>(UIEventResult::Consumed);
    FixedResultWidget* lowerHandledPtr = lowerHandled.get();
    FixedResultWidget* topConsumedPtr = topConsumed.get();
    consumedRoot.addChild(std::move(lowerHandled));
    consumedRoot.addChild(std::move(topConsumed));
    if (consumedRoot.onInput({UIInputEventType::PointerDown, 0.0f, 0.0f, UIPointerButton::Primary}, context) != UIEventResult::Consumed) {
        return fail("UIWidget should stop dispatch when a child consumes input");
    }
    if (topConsumedPtr->callCount() != 1 || lowerHandledPtr->callCount() != 0) {
        return fail("UIWidget should not dispatch to lower children after Consumed");
    }

    TestScene scene;
    auto sceneWidget = std::make_unique<ContextAwareWidget>();
    ContextAwareWidget* sceneWidgetPtr = sceneWidget.get();
    scene.addRoot(std::move(sceneWidget));

    if (scene.onInput({UIInputEventType::PointerMove, 0.0f, 0.0f, UIPointerButton::None}) != UIEventResult::Ignored) {
        return fail("UIScene should ignore input before any context is set");
    }
    if (sceneWidgetPtr->callCount() != 0) {
        return fail("UIScene should not dispatch input without context");
    }

    scene.setInputContext(context);
    if (scene.onInput({UIInputEventType::PointerMove, 1.0f, 1.0f, UIPointerButton::None}) != UIEventResult::Handled) {
        return fail("UIScene should dispatch input with an explicit context");
    }
    if (sceneWidgetPtr->callCount() != 1) {
        return fail("UIScene should dispatch to roots once context exists");
    }

    auto handledRoot = std::make_unique<FixedResultWidget>(UIEventResult::Handled);
    auto consumedRootWidget = std::make_unique<FixedResultWidget>(UIEventResult::Consumed);
    FixedResultWidget* handledRootPtr = handledRoot.get();
    FixedResultWidget* consumedRootPtr = consumedRootWidget.get();
    scene.addRoot(std::move(handledRoot));
    scene.addRoot(std::move(consumedRootWidget));
    if (scene.onInput({UIInputEventType::PointerUp, 0.0f, 0.0f, UIPointerButton::Primary}) != UIEventResult::Consumed) {
        return fail("UIScene should return Consumed when top-most root consumes input");
    }
    if (consumedRootPtr->callCount() != 1 || handledRootPtr->callCount() != 0) {
        return fail("UIScene should stop root dispatch after Consumed");
    }

    std::cout << "[ui_widget_scene_test] PASS\n";
    return EXIT_SUCCESS;
}
