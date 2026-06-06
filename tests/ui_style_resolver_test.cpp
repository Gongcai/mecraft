#include <cmath>
#include <cstdlib>
#include <iostream>

#include "../src/ui/core/UIStyle.h"

namespace {

int fail(const char* message) {
    std::cerr << "[ui_style_resolver_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

bool colorEqual(const Color& a, const Color& b) {
    for (int i = 0; i < 4; ++i) {
        if (std::fabs(a[static_cast<std::size_t>(i)] - b[static_cast<std::size_t>(i)]) > 0.001f) {
            return false;
        }
    }
    return true;
}

} // namespace

int main() {
    UIComponentStyle style;
    style.backgroundNormal = {0.1f, 0.1f, 0.1f, 1.0f};
    style.backgroundHover = {0.2f, 0.2f, 0.2f, 1.0f};
    style.backgroundPressed = {0.3f, 0.3f, 0.3f, 1.0f};
    style.backgroundDisabled = {0.4f, 0.4f, 0.4f, 0.5f};
    style.borderNormal = {0.5f, 0.5f, 0.5f, 1.0f};
    style.borderFocused = {0.6f, 0.6f, 0.6f, 1.0f};
    style.borderDisabled = {0.7f, 0.7f, 0.7f, 0.5f};
    style.textNormal = {0.8f, 0.8f, 0.8f, 1.0f};
    style.textDisabled = {0.9f, 0.9f, 0.9f, 0.5f};
    style.borderWidth = 3.0f;

    const UIResolvedStyle hovered = UIStyleResolver::resolve(style, UIStyleState_Hovered);
    if (!colorEqual(hovered.background, style.backgroundHover) ||
        !colorEqual(hovered.text, style.textNormal) ||
        std::fabs(hovered.borderWidth - 3.0f) > 0.001f) {
        return fail("hover state should resolve hover background and preserve text/border width");
    }

    const int pressedFocusedState = static_cast<int>(UIStyleState_Hovered) |
                                    static_cast<int>(UIStyleState_Pressed) |
                                    static_cast<int>(UIStyleState_Focused);
    const UIResolvedStyle pressedFocused = UIStyleResolver::resolve(style, pressedFocusedState);
    if (!colorEqual(pressedFocused.background, style.backgroundPressed) ||
        !colorEqual(pressedFocused.border, style.borderFocused)) {
        return fail("pressed should win background while focused wins border");
    }

    const int disabledState = static_cast<int>(UIStyleState_Disabled) |
                              static_cast<int>(UIStyleState_Hovered) |
                              static_cast<int>(UIStyleState_Focused);
    const UIResolvedStyle disabled = UIStyleResolver::resolve(style, disabledState);
    if (!colorEqual(disabled.background, style.backgroundDisabled) ||
        !colorEqual(disabled.border, style.borderDisabled) ||
        !colorEqual(disabled.text, style.textDisabled)) {
        return fail("disabled state should override interactive states");
    }

    std::cout << "[ui_style_resolver_test] PASS\n";
    return EXIT_SUCCESS;
}
