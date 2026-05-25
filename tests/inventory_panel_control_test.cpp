#include <cstdlib>
#include <iostream>

#include "../src/ui/inventory/InventoryPanelControl.h"
#include "../src/player/Inventory.h"

namespace {
int fail(const char* message) {
    std::cerr << "[inventory_panel_control_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}
}

int main() {
    InventoryPanelControl panel;
    Inventory inventory;
    UIRenderContext ctx{};

    if (panel.visible) {
        return fail("panel should default to hidden");
    }

    // Smoke: no-op render and input while hidden.
    panel.render(ctx);
    if (panel.onInput({UIInputEventType::PointerMove, 10.0f, 10.0f, UIPointerButton::None}, ctx) != UIEventResult::Ignored) {
        return fail("hidden panel should ignore input");
    }

    panel.setVisible(true);
    panel.setInventorySource(&inventory);
    InventoryPanelLayout layout;
    layout.anchorX = 0.0f;
    layout.anchorY = 0.0f;
    layout.offsetX = 0.0f;
    layout.offsetY = 0.0f;
    layout.panelScale = 1.0f;
    layout.gridOffsetX = 0.0f;
    layout.gridOffsetY = 0.0f;
    layout.slotSize = 10.0f;
    layout.columnGap = 0.0f;
    layout.rowGap = 0.0f;
    layout.row4ExtraGap = 20.0f;
    panel.setLayout(layout);

    if (panel.onInput({UIInputEventType::PointerMove, 2.0f, 2.0f, UIPointerButton::None}, ctx) == UIEventResult::Ignored) {
        return fail("row 1 slot should be hit");
    }

    if (panel.onInput({UIInputEventType::PointerMove, 2.0f, 35.0f, UIPointerButton::None}, ctx) != UIEventResult::Ignored) {
        return fail("gap between row 3 and row 4 should not be hit");
    }

    if (panel.onInput({UIInputEventType::PointerMove, 22.0f, 55.0f, UIPointerButton::None}, ctx) == UIEventResult::Ignored) {
        return fail("row 4 slot should be hit");
    }

    if (panel.onInput({UIInputEventType::PointerDown, 22.0f, 55.0f, UIPointerButton::Primary}, ctx) != UIEventResult::Consumed) {
        return fail("row 4 slot click should be consumed");
    }

    if (panel.onInput({UIInputEventType::PointerUp, 22.0f, 55.0f, UIPointerButton::Primary}, ctx) == UIEventResult::Ignored) {
        return fail("row 4 slot release should be routed");
    }

    if (panel.itemGrid().getLastActivatedIndex() != 29) {
        return fail("row 4 col 3 should map to slot index 29");
    }

    panel.shutdown();

    std::cout << "[inventory_panel_control_test] PASS\n";
    return EXIT_SUCCESS;
}
