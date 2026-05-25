#include <cstdlib>
#include <iostream>

#include "../src/ui/hud/CommandInputOverlay.h"
#include "../src/ui/font/TextRenderer.h"

namespace {
int fail(const char* message) {
    std::cerr << "[command_input_overlay_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}
}

int main() {
    CommandInputOverlay overlay;
    TextRenderer textRenderer;
    UIRenderContext ctx{};

    if (overlay.visible) {
        return fail("command input should default to hidden");
    }
    overlay.setText("/help");
    if (overlay.getText() != "/help") {
        return fail("command input text setter/getter mismatch");
    }
    if (overlay.onInput({UIInputEventType::PointerMove, 0.0f, 0.0f, UIPointerButton::None}, ctx) != UIEventResult::Ignored) {
        return fail("command input overlay should ignore pointer input");
    }

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

