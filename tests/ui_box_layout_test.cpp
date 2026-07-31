#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>

#include "../src/ui/layout/UIBoxLayout.h"

namespace {

int fail(const char* message) {
    std::cerr << "[ui_box_layout_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

bool almostEqual(float a, float b) {
    return std::fabs(a - b) < 0.001f;
}

std::unique_ptr<UIWidget> widget(float w, float h) {
    auto item = std::make_unique<UIWidget>();
    item->width = w;
    item->height = h;
    return item;
}

} // namespace

int main() {
    {
        UIBoxLayout box;
        box.width = 200.0f;
        box.height = 120.0f;
        box.setDirection(UIBoxDirection::Vertical);
        box.setPadding({10.0f, 12.0f, 14.0f, 16.0f});
        box.setGap(6.0f);
        box.setAlignItems(UIAlignItems::Stretch);

        auto first = widget(40.0f, 20.0f);
        auto second = widget(50.0f, 30.0f);
        UIWidget* firstPtr = first.get();
        UIWidget* secondPtr = second.get();
        box.addChild(std::move(first));
        box.addChild(std::move(second));

        box.layout();

        if (!almostEqual(firstPtr->x, 10.0f) || !almostEqual(firstPtr->y, 120.0f - 12.0f - 20.0f) ||
            !almostEqual(firstPtr->width, 176.0f)) {
            return fail("vertical box should place first visible child at top and stretch width");
        }
        if (!almostEqual(secondPtr->x, 10.0f) || !almostEqual(secondPtr->y, 120.0f - 12.0f - 20.0f - 6.0f - 30.0f) ||
            !almostEqual(secondPtr->width, 176.0f)) {
            return fail("vertical box should apply gap and preserve child height");
        }
    }

    {
        UIBoxLayout box;
        box.width = 300.0f;
        box.height = 80.0f;
        box.setDirection(UIBoxDirection::Horizontal);
        box.setPadding(10.0f, 8.0f);
        box.setGap(5.0f);
        box.setAlignItems(UIAlignItems::Center);

        auto first = widget(40.0f, 20.0f);
        auto flex = widget(0.0f, 30.0f);
        auto last = widget(30.0f, 20.0f);
        UIWidget* firstPtr = first.get();
        UIWidget* flexPtr = flex.get();
        UIWidget* lastPtr = last.get();
        box.addChild(std::move(first));
        box.addChild(std::move(flex));
        box.addChild(std::move(last));
        box.setChildFlexGrow(flexPtr, 1.0f);

        box.layout();

        if (!almostEqual(firstPtr->x, 10.0f) || !almostEqual(firstPtr->y, 30.0f)) {
            return fail("horizontal box should honor padding and center alignment");
        }
        if (!almostEqual(flexPtr->x, 55.0f) || !almostEqual(flexPtr->width, 200.0f)) {
            return fail("horizontal box should assign remaining width to flex child");
        }
        if (!almostEqual(lastPtr->x, 260.0f)) {
            return fail("horizontal box should position fixed child after flex child");
        }
    }

    std::cout << "[ui_box_layout_test] PASS\n";
    return EXIT_SUCCESS;
}
