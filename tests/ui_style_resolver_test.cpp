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

    UITheme theme;
    theme.inputBackground = {0.11f, 0.12f, 0.13f, 0.9f};
    theme.inputBorder = {0.21f, 0.22f, 0.23f, 0.7f};
    theme.inputBorderFocused = {0.31f, 0.32f, 0.33f, 1.0f};
    theme.inputText = {0.41f, 0.42f, 0.43f, 1.0f};
    theme.inputPlaceholder = {0.51f, 0.52f, 0.53f, 0.8f};
    theme.inputSelection = {0.61f, 0.62f, 0.63f, 0.4f};
    theme.inputCursor = {0.71f, 0.72f, 0.73f, 0.9f};
    theme.panelBackground = {0.81f, 0.82f, 0.83f, 0.85f};
    theme.panelBorder = {0.91f, 0.92f, 0.93f, 0.7f};
    theme.panelBorderWidth = 4.0f;
    theme.toggleTrackOff = {0.12f, 0.22f, 0.32f, 0.9f};
    theme.toggleTrackOn = {0.42f, 0.52f, 0.62f, 1.0f};
    theme.toggleKnob = {0.72f, 0.73f, 0.74f, 1.0f};
    theme.toggleKnobHover = {0.82f, 0.83f, 0.84f, 1.0f};
    theme.toggleWidth = 48.0f;
    theme.toggleHeight = 24.0f;
    theme.checkboxBox = {0.13f, 0.23f, 0.33f, 0.9f};
    theme.checkboxBoxHover = {0.43f, 0.53f, 0.63f, 1.0f};
    theme.checkboxBoxBorder = {0.73f, 0.74f, 0.75f, 0.5f};
    theme.checkboxCheck = {0.83f, 0.84f, 0.85f, 1.0f};
    theme.checkboxSize = 26.0f;
    theme.sliderTrack = {0.14f, 0.24f, 0.34f, 0.9f};
    theme.sliderFill = {0.44f, 0.54f, 0.64f, 1.0f};
    theme.sliderHandle = {0.74f, 0.75f, 0.76f, 1.0f};
    theme.sliderHandleHover = {0.84f, 0.85f, 0.86f, 1.0f};
    theme.sliderTrackHeight = 6.0f;
    theme.sliderHandleSize = 18.0f;
    theme.progressTrack = {0.15f, 0.25f, 0.35f, 0.9f};
    theme.progressFill = {0.45f, 0.55f, 0.65f, 1.0f};
    theme.progressText = {0.75f, 0.76f, 0.77f, 1.0f};

    const UIComponentStyle panelStyle = UIStyleResolver::panelStyleFromTheme(&theme);
    const UIResolvedStyle panel = UIStyleResolver::resolve(panelStyle, UIStyleState_Normal);
    if (!colorEqual(panel.background, theme.panelBackground) ||
        !colorEqual(panel.border, theme.panelBorder) ||
        std::fabs(panel.borderWidth - 4.0f) > 0.001f) {
        return fail("panel style should map background, border, and border width from theme");
    }

    const UITextInputStyle inputStyle = UIStyleResolver::textInputStyleFromTheme(&theme);
    const UIResolvedTextInputStyle focusedInput =
        UIStyleResolver::resolveTextInput(inputStyle, UIStyleState_Focused);
    if (!colorEqual(focusedInput.frame.background, theme.inputBackground) ||
        !colorEqual(focusedInput.frame.border, theme.inputBorderFocused) ||
        !colorEqual(focusedInput.frame.text, theme.inputText) ||
        !colorEqual(focusedInput.placeholder, theme.inputPlaceholder) ||
        !colorEqual(focusedInput.selection, theme.inputSelection) ||
        !colorEqual(focusedInput.cursor, theme.inputCursor) ||
        std::fabs(focusedInput.frame.borderWidth - 4.0f) > 0.001f) {
        return fail("text input style should map focused frame and auxiliary colors from theme");
    }

    const UIResolvedTextInputStyle disabledInput =
        UIStyleResolver::resolveTextInput(inputStyle, UIStyleState_Disabled);
    if (!colorEqual(disabledInput.frame.text, theme.textDisabled) ||
        !colorEqual(disabledInput.placeholder, theme.textDisabled) ||
        !(disabledInput.selection[3] < theme.inputSelection[3]) ||
        !(disabledInput.cursor[3] < theme.inputCursor[3])) {
        return fail("disabled text input should use disabled text and muted selection/cursor colors");
    }

    const UIToggleStyle toggleStyle = UIStyleResolver::toggleStyleFromTheme(&theme);
    const UIResolvedToggleStyle hoveredToggle =
        UIStyleResolver::resolveToggle(toggleStyle, UIStyleState_Hovered);
    if (!colorEqual(hoveredToggle.trackOff, theme.toggleTrackOff) ||
        !colorEqual(hoveredToggle.trackOn, theme.toggleTrackOn) ||
        !colorEqual(hoveredToggle.knob, theme.toggleKnobHover) ||
        !colorEqual(hoveredToggle.text, theme.textPrimary) ||
        std::fabs(hoveredToggle.width - 48.0f) > 0.001f ||
        std::fabs(hoveredToggle.height - 24.0f) > 0.001f) {
        return fail("toggle style should map theme colors, hover knob, and dimensions");
    }

    const UIResolvedToggleStyle disabledToggle =
        UIStyleResolver::resolveToggle(toggleStyle, UIStyleState_Disabled | UIStyleState_Hovered);
    if (!colorEqual(disabledToggle.text, theme.textDisabled) ||
        colorEqual(disabledToggle.knob, theme.toggleKnobHover)) {
        return fail("disabled toggle should override hover state");
    }

    const UICheckboxStyle checkboxStyle = UIStyleResolver::checkboxStyleFromTheme(&theme);
    const UIResolvedCheckboxStyle hoveredCheckbox =
        UIStyleResolver::resolveCheckbox(checkboxStyle, UIStyleState_Hovered);
    if (!colorEqual(hoveredCheckbox.box, theme.checkboxBoxHover) ||
        !colorEqual(hoveredCheckbox.border, theme.checkboxBoxBorder) ||
        !colorEqual(hoveredCheckbox.check, theme.checkboxCheck) ||
        !colorEqual(hoveredCheckbox.text, theme.textPrimary) ||
        std::fabs(hoveredCheckbox.boxSize - 26.0f) > 0.001f ||
        std::fabs(hoveredCheckbox.borderWidth - 1.0f) > 0.001f) {
        return fail("checkbox style should map hover box, border, check, text, and size");
    }

    const UIResolvedCheckboxStyle disabledCheckbox =
        UIStyleResolver::resolveCheckbox(checkboxStyle, UIStyleState_Disabled | UIStyleState_Hovered);
    if (!colorEqual(disabledCheckbox.text, theme.textDisabled) ||
        colorEqual(disabledCheckbox.box, theme.checkboxBoxHover)) {
        return fail("disabled checkbox should override hover state");
    }

    const UISliderStyle sliderStyle = UIStyleResolver::sliderStyleFromTheme(&theme);
    const UIResolvedSliderStyle hoveredSlider =
        UIStyleResolver::resolveSlider(sliderStyle, UIStyleState_Hovered);
    if (!colorEqual(hoveredSlider.track, theme.sliderTrack) ||
        !colorEqual(hoveredSlider.fill, theme.sliderFill) ||
        !colorEqual(hoveredSlider.handle, theme.sliderHandleHover) ||
        std::fabs(hoveredSlider.trackHeight - 6.0f) > 0.001f ||
        std::fabs(hoveredSlider.handleSize - 18.0f) > 0.001f) {
        return fail("slider style should map theme colors, hover handle, and dimensions");
    }

    const UIResolvedSliderStyle disabledSlider =
        UIStyleResolver::resolveSlider(sliderStyle, UIStyleState_Disabled | UIStyleState_Hovered);
    if (!colorEqual(disabledSlider.track, theme.buttonDisabled) ||
        !colorEqual(disabledSlider.fill, theme.buttonBorder) ||
        !colorEqual(disabledSlider.handle, theme.textDisabled) ||
        colorEqual(disabledSlider.handle, theme.sliderHandleHover)) {
        return fail("disabled slider should override hover state");
    }

    const UIProgressBarStyle progressBarStyle = UIStyleResolver::progressBarStyleFromTheme(&theme);
    const UIResolvedProgressBarStyle progressBar = UIStyleResolver::resolveProgressBar(progressBarStyle);
    if (!colorEqual(progressBar.track, theme.progressTrack) ||
        !colorEqual(progressBar.fill, theme.progressFill) ||
        !colorEqual(progressBar.text, theme.progressText)) {
        return fail("progress bar style should map track, fill, and text colors from theme");
    }

    std::cout << "[ui_style_resolver_test] PASS\n";
    return EXIT_SUCCESS;
}
