#include <cstdlib>
#include <iostream>

#include "../src/ui/CreativeInventoryPanelControl.h"

namespace {
int fail(const char* message) {
    std::cerr << "[creative_inventory_panel_control_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}
}

int main() {
    CreativeInventoryPanelControl panel;
    UIRenderContext ctx{};
    ctx.screenWidth = 400;
    ctx.screenHeight = 300;

    if (panel.visible) {
        return fail("panel should default to hidden");
    }
    if (panel.getTab() != CreativeInventoryTab::PlayerInventory) {
        return fail("default tab should be player inventory");
    }
    if (panel.onInput({UIInputEventType::PointerMove, 10.0f, 10.0f, UIPointerButton::None}, ctx) != UIEventResult::Ignored) {
        return fail("hidden panel should ignore input");
    }

    CreativeInventoryLayout layout;
    layout.panelScale = 1.0f;
    panel.setLayout(layout);
    panel.setVisible(true);

    // Main panel is centered at x=102.5 y=82 in this reference context.
    const UIInputEvent clickAllItemsTab{
        UIInputEventType::PointerDown,
        110.0f,
        60.0f,
        UIPointerButton::Primary
    };
    if (panel.onInput(clickAllItemsTab, ctx) != UIEventResult::Consumed) {
        return fail("clicking all items tab should be consumed");
    }
    if (panel.getTab() != CreativeInventoryTab::AllItems) {
        return fail("all items tab should be selected");
    }

    ItemID manyItems[64]{};
    for (int i = 0; i < 64; ++i) {
        manyItems[i] = static_cast<ItemID>(i + 1);
    }
    panel.setCreativeItemsForTest(manyItems, 64);
    if (!panel.isScrollerEnabledForTest()) {
        return fail("scroller should be enabled when creative items exceed one page");
    }

    UIInputEvent scrollDown{};
    scrollDown.type = UIInputEventType::Scroll;
    scrollDown.x = 180.0f;
    scrollDown.y = 120.0f;
    scrollDown.scrollY = -1.0f;
    if (panel.onInput(scrollDown, ctx) != UIEventResult::Consumed) {
        return fail("scrolling enabled creative list should be consumed");
    }
    if (panel.getScrollRowForTest() != 1) {
        return fail("scroll down should increment scroll row");
    }
    for (int i = 0; i < 20; ++i) {
        panel.onInput(scrollDown, ctx);
    }
    if (panel.getScrollRowForTest() != 3) {
        return fail("scroll row should clamp to max row");
    }

    ItemID fewItems[4] = {1, 2, 3, 4};
    panel.setCreativeItemsForTest(fewItems, 4);
    if (panel.isScrollerEnabledForTest()) {
        return fail("scroller should be disabled for less than one page");
    }
    if (panel.getScrollRowForTest() != 0) {
        return fail("scroll row should clamp back to zero");
    }
    if (panel.onInput(scrollDown, ctx) != UIEventResult::Handled) {
        return fail("disabled scroller should handle but not consume scroll");
    }
    if (panel.getScrollRowForTest() != 0) {
        return fail("disabled scroller should not change scroll row");
    }

    ItemID clickItems[46]{};
    for (int i = 0; i < 46; ++i) {
        clickItems[i] = static_cast<ItemID>(100 + i);
    }
    panel.setCreativeItemsForTest(clickItems, 46);
    const UIInputEvent clickFirstCreativeSlot{
        UIInputEventType::PointerDown,
        113.0f,
        103.0f,
        UIPointerButton::Primary
    };
    if (panel.onInput(clickFirstCreativeSlot, ctx) != UIEventResult::Consumed) {
        return fail("clicking creative item slot should be consumed");
    }
    if (panel.getLastActivatedCreativeItem() != 100) {
        return fail("activated creative item should match first visible item");
    }

    const UIInputEvent clickPlayerInventoryTab{
        UIInputEventType::PointerDown,
        270.0f,
        220.0f,
        UIPointerButton::Primary
    };
    if (panel.onInput(clickPlayerInventoryTab, ctx) != UIEventResult::Consumed) {
        return fail("clicking player inventory tab should be consumed");
    }
    if (panel.getTab() != CreativeInventoryTab::PlayerInventory) {
        return fail("player inventory tab should be selected");
    }

    panel.shutdown();
    std::cout << "[creative_inventory_panel_control_test] PASS\n";
    return EXIT_SUCCESS;
}
