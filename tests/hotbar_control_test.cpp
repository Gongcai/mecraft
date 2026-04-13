#include <cstdlib>
#include <iostream>

#include "../src/ui/HotbarControl.h"

namespace {
int fail(const char* message) {
    std::cerr << "[hotbar_control_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}
}

int main() {
    HotbarControl hotbar;

    if (!hotbar.isVisible()) {
        return fail("hotbar should default to visible");
    }
    if (hotbar.onInput({UIInputEventType::PointerMove, 0.0f, 0.0f, UIPointerButton::None}) != UIEventResult::Ignored) {
        return fail("hotbar should ignore pointer input by default");
    }

    const std::array<float, 4> bg{0.1f, 0.2f, 0.3f, 0.4f};
    const std::array<float, 4> border{0.5f, 0.6f, 0.7f, 0.8f};
    const std::array<float, 4> icon{0.9f, 0.7f, 0.5f, 0.3f};

    hotbar.setBgColor(bg);
    hotbar.setBorderColor(border);
    hotbar.setIconTintColor(icon);

    if (hotbar.getBgColor() != bg) {
        return fail("bg color setter/getter mismatch");
    }
    if (hotbar.getBorderColor() != border) {
        return fail("border color setter/getter mismatch");
    }
    if (hotbar.getIconTintColor() != icon) {
        return fail("icon tint setter/getter mismatch");
    }

    hotbar.setCountTextScale(0.42f);
    if (hotbar.getCountTextScale() != 0.42f) {
        return fail("count text scale setter/getter mismatch");
    }

    // Smoke: should be safe without init.
    hotbar.shutdown();

    std::cout << "[hotbar_control_test] PASS\n";
    return EXIT_SUCCESS;
}

