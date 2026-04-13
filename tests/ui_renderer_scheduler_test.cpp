#include <cstdlib>
#include <iostream>

#include "../src/ui/UIRenderer.h"

namespace {
int fail(const char* message) {
    std::cerr << "[ui_renderer_scheduler_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}
}

int main() {
    UIRenderer renderer;

    // Smoke/API: routing without init should be safe and ignored.
    if (renderer.routeUIInput({UIInputEventType::PointerMove, 8.0f, 8.0f, UIPointerButton::None}) != UIEventResult::Ignored) {
        return fail("routeUIInput should ignore when no visible controls are active");
    }

    renderer.setInventoryPanelVisible(true);
    if (renderer.routeUIInput({UIInputEventType::PointerMove, 8.0f, 8.0f, UIPointerButton::None}) != UIEventResult::Ignored) {
        return fail("routeUIInput should still ignore before controls are initialized");
    }

    InventoryPanelLayout layout = renderer.getInventoryPanelLayout();
    layout.row4ExtraGap = 24.0f;
    renderer.setInventoryPanelLayout(layout);
    if (renderer.getInventoryPanelLayout().row4ExtraGap != 24.0f) {
        return fail("inventory panel layout setter/getter should round-trip");
    }

    renderer.setInventoryPanelVisible(false);
    renderer.shutdown();

    std::cout << "[ui_renderer_scheduler_test] PASS\n";
    return EXIT_SUCCESS;
}

