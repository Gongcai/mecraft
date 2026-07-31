#include <cstdlib>
#include <iostream>

#include "../src/ui/hud/ConsoleOverlay.h"
#include "../src/ui/font/TextRenderer.h"

namespace {
int fail(const char* message) {
    std::cerr << "[console_overlay_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}
} // namespace

int main() {
    ConsoleOverlay console;
    TextRenderer textRenderer;
    UIRenderContext ctx{};
    ctx.textRenderer = &textRenderer;

    if (!console.visible) {
        return fail("console should default to visible");
    }
    if (console.onInput({UIInputEventType::PointerMove, 0.0f, 0.0f, UIPointerButton::None}, ctx) !=
        UIEventResult::Ignored) {
        return fail("console should ignore pointer input");
    }

    if (!console.empty()) {
        return fail("console should start empty");
    }

    console.appendLine("hello", 1.0, ConsoleDisplayBox::MessageType::Normal);
    if (console.empty()) {
        return fail("appendLine should add an entry");
    }

    console.clear();
    if (!console.empty()) {
        return fail("clear should remove all lines");
    }

    // Smoke: render with empty queue should no-op without init/context.
    console.render(ctx);
    console.shutdown();

    std::cout << "[console_overlay_test] PASS\n";
    return EXIT_SUCCESS;
}
