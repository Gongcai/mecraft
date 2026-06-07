#include <cmath>
#include <cstdlib>
#include <iostream>

#include "../src/ui/widgets/UIScrollArea.h"

namespace {

class TestScrollArea final : public UIScrollArea {
public:
    using UIScrollArea::onInput;
};

int fail(const char* message) {
    std::cerr << "[ui_scroll_area_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

UIInputEvent pointerEvent(UIInputEventType type, float x, float y) {
    UIInputEvent event;
    event.type = type;
    event.x = x;
    event.y = y;
    event.button = type == UIInputEventType::PointerDown ? UIPointerButton::Primary : UIPointerButton::None;
    return event;
}

} // namespace

int main() {
    UIRenderContext ctx{};
    ctx.screenWidth = 200;
    ctx.screenHeight = 200;

    TestScrollArea area;
    area.anchor = Anchor::BottomLeft;
    area.width = 100.0f;
    area.height = 100.0f;
    area.setContentHeight(300.0f);

    UIInputEvent wheelDown;
    wheelDown.type = UIInputEventType::Scroll;
    wheelDown.x = 50.0f;
    wheelDown.y = 150.0f;
    wheelDown.scrollY = -1.0f;
    if (area.onInput(wheelDown, ctx) != UIEventResult::Handled) {
        return fail("wheel scroll inside area should be handled");
    }
    if (std::fabs(area.getScrollOffset() - 40.0f) > 0.001f) {
        return fail("wheel down should increase scroll offset");
    }

    area.setScrollOffset(0.0f);

    constexpr float kThumbCenterScreenYAtTop = 200.0f - (100.0f - (100.0f / 300.0f * 100.0f) * 0.5f);
    constexpr float kThumbTravel = 100.0f - (100.0f / 300.0f * 100.0f);
    constexpr float kDragDistance = 30.0f;
    constexpr float kExpectedOffset = (kDragDistance / kThumbTravel) * 200.0f;

    if (area.onInput(pointerEvent(UIInputEventType::PointerDown, 96.0f, kThumbCenterScreenYAtTop), ctx) !=
        UIEventResult::Consumed) {
        return fail("pointer down on top-positioned thumb should start drag");
    }
    if (area.onInput(pointerEvent(UIInputEventType::PointerMove, 96.0f, kThumbCenterScreenYAtTop + kDragDistance), ctx) !=
        UIEventResult::Consumed) {
        return fail("pointer move while dragging should be consumed");
    }
    if (std::fabs(area.getScrollOffset() - kExpectedOffset) > 0.001f) {
        return fail("dragging thumb downward should increase scroll offset proportionally");
    }

    if (area.onInput(pointerEvent(UIInputEventType::PointerUp, 96.0f, kThumbCenterScreenYAtTop + kDragDistance), ctx) !=
        UIEventResult::Consumed) {
        return fail("pointer up should finish scrollbar drag");
    }

    return EXIT_SUCCESS;
}
