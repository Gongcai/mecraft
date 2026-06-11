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

    if (!almostEqual(UIRenderer::computeResponsiveUiScale(1280.0f, 720.0f), 1.0f)) {
        return fail("reference resolution should use 1x UI scale");
    }
    if (!almostEqual(UIRenderer::computeResponsiveUiScale(640.0f, 360.0f), 0.5f)) {
        return fail("smaller matching windows should downscale UI");
    }
    if (!almostEqual(UIRenderer::computeResponsiveUiScale(1920.0f, 1080.0f), 1.5f)) {
        return fail("larger matching windows should upscale UI");
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

