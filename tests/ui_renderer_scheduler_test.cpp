#include <cmath>
#include <cstdlib>
#include <iostream>

#include "../src/ui/core/UIRenderer.h"

namespace {
int fail(const char* message) {
    std::cerr << "[ui_renderer_scheduler_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

bool almostEqual(float a, float b) {
    return std::fabs(a - b) < 0.001f;
}
}

int main() {
    UIRenderer renderer;

    const UIScaleConfig hdScale = UIScaleConfig::create(1280.0f, 720.0f, GUIScale::Auto);
    if (!almostEqual(hdScale.effectiveScale, 1.0f) || hdScale.virtualWidth != 1280 || hdScale.virtualHeight != 720) {
        return fail("720p auto GUI scale should use 1x virtual coordinates");
    }
    const UIScaleConfig smallScale = UIScaleConfig::create(640.0f, 360.0f, GUIScale::Auto);
    if (!almostEqual(smallScale.effectiveScale, 0.5f) || smallScale.virtualWidth != 1280 || smallScale.virtualHeight != 720) {
        return fail("sub-720p auto GUI scale should preserve readable virtual coordinates");
    }
    const UIScaleConfig normalScale = UIScaleConfig::create(1920.0f, 1080.0f, GUIScale::Normal);
    if (!almostEqual(normalScale.effectiveScale, 1.0f) || normalScale.virtualWidth != 1920 || normalScale.virtualHeight != 1080) {
        return fail("explicit normal GUI scale should use 1x coordinates");
    }

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

