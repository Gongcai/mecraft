#include <cstdlib>
#include <iostream>

#include "../src/ui/CommandInputOverlay.h"
#include "../src/ui/TextRenderer.h"

namespace {
int fail(const char* message) {
    std::cerr << "[command_input_overlay_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}
}

int main() {
    CommandInputOverlay overlay;
    TextRenderer textRenderer;

    overlay.setCaretBlinkPeriodMs(1.0f);
    if (overlay.getCaretBlinkPeriodMs() != 120.0f) {
        return fail("caret period should clamp to minimum");
    }

    overlay.setCaretBlinkPeriodMs(9999.0f);
    if (overlay.getCaretBlinkPeriodMs() != 2500.0f) {
        return fail("caret period should clamp to maximum");
    }

    // Smoke: should be safe without init.
    overlay.render("/gamemode creative", textRenderer);
    overlay.shutdown();

    std::cout << "[command_input_overlay_test] PASS\n";
    return EXIT_SUCCESS;
}

