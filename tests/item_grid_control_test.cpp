#include <cstdlib>
#include <iostream>

#include "../src/ui/ItemGridControl.h"

namespace {
int fail(const char* message) {
    std::cerr << "[item_grid_control_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}
}

int main() {
    ItemGridControl control;

    // Smoke: should no-op even before init.
    control.setVisible(true);
    UIRenderContext ctx{};

    const Pickable::SlotInfo slots[] = {
        {10, 10, 20, 1},
        {40, 10, 20, 2},
    };
    control.setSlots(slots, 2);

    if (control.onInput({UIInputEventType::PointerMove, 12.0f, 12.0f, UIPointerButton::None}, ctx) != UIEventResult::Handled) {
        return fail("pointer move over slot should be handled");
    }
    if (control.getHoveredIndex() != 0) {
        return fail("hovered index should be first slot");
    }

    if (control.onInput({UIInputEventType::PointerDown, 12.0f, 12.0f, UIPointerButton::Primary}, ctx) != UIEventResult::Consumed) {
        return fail("left click inside slot should be consumed");
    }
    if (control.getLastActivatedIndex() != 0) {
        return fail("activated index should be first slot");
    }

    if (control.onInput({UIInputEventType::PointerUp, 12.0f, 12.0f, UIPointerButton::Primary}, ctx) != UIEventResult::Handled) {
        return fail("pointer up over slot should be handled");
    }

    if (control.onInput({UIInputEventType::PointerMove, 0.0f, 0.0f, UIPointerButton::None}, ctx) != UIEventResult::Ignored) {
        return fail("pointer move outside slots should be ignored");
    }

    control.shutdown();

    std::cout << "[item_grid_control_test] PASS\n";
    return EXIT_SUCCESS;
}

