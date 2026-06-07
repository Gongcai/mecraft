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
    theme.buttonNormal = {0.10f, 0.20f, 0.30f, 0.92f};
    theme.buttonHover = {0.20f, 0.30f, 0.40f, 1.0f};
    theme.buttonDisabled = {0.08f, 0.09f, 0.10f, 0.5f};
    theme.buttonBorder = {0.30f, 0.40f, 0.50f, 0.4f};
    theme.buttonBorderWidth = 5.0f;
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
    theme.radioOuter = {0.16f, 0.26f, 0.36f, 0.9f};
    theme.radioOuterHover = {0.46f, 0.56f, 0.66f, 1.0f};
    theme.radioInner = {0.76f, 0.77f, 0.78f, 1.0f};
    theme.radioSize = 22.0f;
    theme.dropdownBackground = {0.17f, 0.27f, 0.37f, 0.95f};
    theme.dropdownBorder = {0.47f, 0.57f, 0.67f, 0.7f};
    theme.dropdownArrow = {0.77f, 0.78f, 0.79f, 1.0f};
    theme.dropdownItemHover = {0.18f, 0.28f, 0.38f, 1.0f};
    theme.dropdownItemSelected = {0.48f, 0.58f, 0.68f, 0.35f};
    theme.dropdownSeparator = {0.78f, 0.79f, 0.80f, 0.4f};
    theme.accentPrimary = {0.19f, 0.29f, 0.39f, 1.0f};
    theme.contextMenuBackground = {0.20f, 0.30f, 0.40f, 0.95f};
    theme.contextMenuBorder = {0.50f, 0.60f, 0.70f, 0.7f};
    theme.contextMenuItemHover = {0.21f, 0.31f, 0.41f, 1.0f};
    theme.contextMenuSeparator = {0.51f, 0.61f, 0.71f, 0.5f};
    theme.contextMenuWidth = 220.0f;
    theme.contextMenuItemHeight = 32.0f;
    theme.scrollbarTrack = {0.22f, 0.32f, 0.42f, 0.6f};
    theme.scrollbarThumb = {0.52f, 0.62f, 0.72f, 0.8f};
    theme.scrollbarThumbHover = {0.82f, 0.83f, 0.84f, 0.9f};
    theme.scrollbarWidth = 10.0f;
    theme.tabHeader = {0.23f, 0.33f, 0.43f, 0.9f};
    theme.tabHeaderActive = {0.53f, 0.63f, 0.73f, 1.0f};
    theme.tabHeaderHover = {0.83f, 0.84f, 0.85f, 1.0f};
    theme.tabIndicator = {0.24f, 0.34f, 0.44f, 1.0f};
    theme.tabContent = {0.54f, 0.64f, 0.74f, 0.85f};
    theme.tabHeaderHeight = 42.0f;
    theme.toastBackground = {0.25f, 0.35f, 0.45f, 0.92f};
    theme.toastText = {0.55f, 0.65f, 0.75f, 1.0f};
    theme.toastInfo = {0.26f, 0.36f, 0.46f, 1.0f};
    theme.toastSuccess = {0.56f, 0.66f, 0.76f, 1.0f};
    theme.toastWarning = {0.86f, 0.87f, 0.88f, 1.0f};
    theme.toastError = {0.27f, 0.37f, 0.47f, 1.0f};
    theme.toastWidth = 340.0f;
    theme.toastHeight = 44.0f;
    theme.tooltipBackground = {0.28f, 0.38f, 0.48f, 0.94f};
    theme.tooltipBorder = {0.58f, 0.68f, 0.78f, 0.8f};
    theme.tooltipBorderWidth = 3.0f;

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

    const UIRadioButtonStyle radioStyle = UIStyleResolver::radioButtonStyleFromTheme(&theme);
    const UIResolvedRadioButtonStyle hoveredRadio =
        UIStyleResolver::resolveRadioButton(radioStyle, UIStyleState_Hovered);
    if (!colorEqual(hoveredRadio.outer, theme.radioOuterHover) ||
        !colorEqual(hoveredRadio.inner, theme.radioInner) ||
        !colorEqual(hoveredRadio.text, theme.textPrimary) ||
        std::fabs(hoveredRadio.radioSize - 22.0f) > 0.001f) {
        return fail("radio button style should map hover outer, inner, text, and size");
    }

    const UIResolvedRadioButtonStyle disabledRadio =
        UIStyleResolver::resolveRadioButton(radioStyle, UIStyleState_Disabled | UIStyleState_Hovered);
    if (!colorEqual(disabledRadio.outer, theme.buttonDisabled) ||
        !colorEqual(disabledRadio.inner, theme.textDisabled) ||
        !colorEqual(disabledRadio.text, theme.textDisabled) ||
        colorEqual(disabledRadio.outer, theme.radioOuterHover)) {
        return fail("disabled radio button should override hover state");
    }

    const UIDropdownStyle dropdownStyle = UIStyleResolver::dropdownStyleFromTheme(&theme);
    const UIResolvedDropdownStyle dropdown = UIStyleResolver::resolveDropdown(dropdownStyle);
    if (!colorEqual(dropdown.background, theme.dropdownBackground) ||
        !colorEqual(dropdown.border, theme.dropdownBorder) ||
        !colorEqual(dropdown.text, theme.textPrimary) ||
        !colorEqual(dropdown.arrow, theme.dropdownArrow) ||
        !colorEqual(dropdown.itemHover, theme.dropdownItemHover) ||
        !colorEqual(dropdown.itemSelected, theme.dropdownItemSelected) ||
        !colorEqual(dropdown.separator, theme.dropdownSeparator) ||
        !colorEqual(dropdown.accent, theme.accentPrimary) ||
        std::fabs(dropdown.itemHeight - 28.0f) > 0.001f) {
        return fail("dropdown style should map theme colors and preserve default item height");
    }

    const UIContextMenuStyle contextMenuStyle = UIStyleResolver::contextMenuStyleFromTheme(&theme);
    const UIResolvedContextMenuStyle contextMenu = UIStyleResolver::resolveContextMenu(contextMenuStyle);
    if (!colorEqual(contextMenu.background, theme.contextMenuBackground) ||
        !colorEqual(contextMenu.border, theme.contextMenuBorder) ||
        !colorEqual(contextMenu.itemHover, theme.contextMenuItemHover) ||
        !colorEqual(contextMenu.separator, theme.contextMenuSeparator) ||
        !colorEqual(contextMenu.text, theme.textPrimary) ||
        std::fabs(contextMenu.width - 220.0f) > 0.001f ||
        std::fabs(contextMenu.borderWidth - 4.0f) > 0.001f ||
        std::fabs(contextMenu.itemHeight - 32.0f) > 0.001f ||
        std::fabs(contextMenu.separatorHeight - 6.0f) > 0.001f ||
        std::fabs(contextMenu.padding - 4.0f) > 0.001f) {
        return fail("context menu style should map theme colors and dimensions");
    }

    const UIScrollAreaStyle scrollAreaStyle = UIStyleResolver::scrollAreaStyleFromTheme(&theme);
    const UIResolvedScrollAreaStyle hoveredScrollArea =
        UIStyleResolver::resolveScrollArea(scrollAreaStyle, UIStyleState_Hovered);
    if (!colorEqual(hoveredScrollArea.track, theme.scrollbarTrack) ||
        !colorEqual(hoveredScrollArea.thumb, theme.scrollbarThumbHover) ||
        std::fabs(hoveredScrollArea.scrollbarWidth - 10.0f) > 0.001f) {
        return fail("scroll area style should map track, hover thumb, and width");
    }

    const UIResolvedScrollAreaStyle disabledScrollArea =
        UIStyleResolver::resolveScrollArea(scrollAreaStyle, UIStyleState_Disabled | UIStyleState_Hovered);
    if (!colorEqual(disabledScrollArea.thumb, theme.textDisabled) ||
        colorEqual(disabledScrollArea.thumb, theme.scrollbarThumbHover)) {
        return fail("disabled scroll area should override hover state");
    }

    const UITabControlStyle tabControlStyle = UIStyleResolver::tabControlStyleFromTheme(&theme);
    const UIResolvedTabControlStyle hoveredTab =
        UIStyleResolver::resolveTabControl(tabControlStyle, UIStyleState_Hovered);
    if (!colorEqual(hoveredTab.header, theme.tabHeaderHover) ||
        !colorEqual(hoveredTab.indicator, theme.tabIndicator) ||
        !colorEqual(hoveredTab.content, theme.tabContent) ||
        !colorEqual(hoveredTab.text, theme.textPrimary) ||
        std::fabs(hoveredTab.headerHeight - 42.0f) > 0.001f ||
        std::fabs(hoveredTab.indicatorHeight - 3.0f) > 0.001f) {
        return fail("tab control style should map hover, shared colors, and dimensions");
    }

    const UIResolvedTabControlStyle selectedTab =
        UIStyleResolver::resolveTabControl(tabControlStyle, UIStyleState_Selected | UIStyleState_Hovered);
    if (!colorEqual(selectedTab.header, theme.tabHeaderActive)) {
        return fail("selected tab should override hover state");
    }

    const UIResolvedTabControlStyle disabledTab =
        UIStyleResolver::resolveTabControl(tabControlStyle, UIStyleState_Disabled | UIStyleState_Selected);
    if (!colorEqual(disabledTab.header, theme.buttonDisabled) ||
        !colorEqual(disabledTab.text, theme.textDisabled) ||
        colorEqual(disabledTab.header, theme.tabHeaderActive)) {
        return fail("disabled tab should override selected state");
    }

    const UINumericSpinnerStyle spinnerStyle = UIStyleResolver::numericSpinnerStyleFromTheme(&theme);
    const UIResolvedNumericSpinnerStyle focusedSpinner =
        UIStyleResolver::resolveNumericSpinner(
            spinnerStyle,
            UIStyleState_Hovered,
            UIStyleState_Normal,
            UIStyleState_Focused);
    if (!colorEqual(focusedSpinner.minusBackground, theme.buttonHover) ||
        !colorEqual(focusedSpinner.minusBorder, theme.buttonBorder) ||
        !colorEqual(focusedSpinner.plusBackground, theme.buttonNormal) ||
        !colorEqual(focusedSpinner.valueBackground, theme.inputBackground) ||
        !colorEqual(focusedSpinner.valueBorder, theme.inputBorderFocused) ||
        !colorEqual(focusedSpinner.text, theme.inputText) ||
        !colorEqual(focusedSpinner.cursor, theme.inputCursor) ||
        std::fabs(focusedSpinner.buttonWidth - 28.0f) > 0.001f ||
        std::fabs(focusedSpinner.gap - 2.0f) > 0.001f ||
        std::fabs(focusedSpinner.borderWidth - 5.0f) > 0.001f ||
        std::fabs(focusedSpinner.textPadding - 6.0f) > 0.001f ||
        std::fabs(focusedSpinner.cursorWidth - 1.5f) > 0.001f ||
        std::fabs(focusedSpinner.cursorInset - 3.0f) > 0.001f) {
        return fail("numeric spinner should compose button/input colors and metrics");
    }

    const UIResolvedNumericSpinnerStyle disabledSpinner =
        UIStyleResolver::resolveNumericSpinner(
            spinnerStyle,
            UIStyleState_Disabled | UIStyleState_Hovered,
            UIStyleState_Disabled,
            UIStyleState_Disabled | UIStyleState_Focused);
    if (!colorEqual(disabledSpinner.minusBackground, theme.buttonDisabled) ||
        !colorEqual(disabledSpinner.valueBorder, theme.buttonBorder) ||
        !colorEqual(disabledSpinner.text, theme.textDisabled) ||
        colorEqual(disabledSpinner.minusBackground, theme.buttonHover)) {
        return fail("disabled numeric spinner zones should override interactive states");
    }

    const UIToastStyle toastStyle = UIStyleResolver::toastStyleFromTheme(&theme);
    const UIResolvedToastStyle warningToast =
        UIStyleResolver::resolveToast(toastStyle, UIToastTone::Warning);
    const Color expectedToastBorder{
        theme.toastBackground[0] * 1.3f,
        theme.toastBackground[1] * 1.3f,
        theme.toastBackground[2] * 1.3f,
        0.5f};
    if (!colorEqual(warningToast.background, theme.toastBackground) ||
        !colorEqual(warningToast.border, expectedToastBorder) ||
        !colorEqual(warningToast.text, theme.toastText) ||
        !colorEqual(warningToast.accent, theme.toastWarning) ||
        std::fabs(warningToast.width - 340.0f) > 0.001f ||
        std::fabs(warningToast.height - 44.0f) > 0.001f ||
        std::fabs(warningToast.spacing - 8.0f) > 0.001f ||
        std::fabs(warningToast.bottomMargin - 60.0f) > 0.001f ||
        std::fabs(warningToast.borderWidth - 1.0f) > 0.001f ||
        std::fabs(warningToast.accentWidth - 3.0f) > 0.001f ||
        std::fabs(warningToast.textPadding - 10.0f) > 0.001f) {
        return fail("toast style should map theme colors, derived border, tone, and metrics");
    }

    const UIResolvedToastStyle errorToast = UIStyleResolver::resolveToast(toastStyle, UIToastTone::Error);
    if (!colorEqual(errorToast.accent, theme.toastError) ||
        colorEqual(errorToast.accent, theme.toastWarning)) {
        return fail("toast tone should select the matching accent color");
    }

    const UITooltipStyle tooltipStyle = UIStyleResolver::tooltipStyleFromTheme(&theme);
    const UIResolvedTooltipStyle tooltip = UIStyleResolver::resolveTooltip(tooltipStyle);
    if (!colorEqual(tooltip.background, theme.tooltipBackground) ||
        !colorEqual(tooltip.border, theme.tooltipBorder) ||
        !colorEqual(tooltip.text, theme.textPrimary) ||
        !colorEqual(tooltip.shadow, Color{0.0f, 0.0f, 0.0f, 0.75f}) ||
        std::fabs(tooltip.borderWidth - 3.0f) > 0.001f ||
        std::fabs(tooltip.textScale - 2.0f) > 0.001f ||
        std::fabs(tooltip.paddingX - 10.0f) > 0.001f ||
        std::fabs(tooltip.paddingY - 6.0f) > 0.001f ||
        std::fabs(tooltip.offsetX - 12.0f) > 0.001f ||
        std::fabs(tooltip.offsetY - 16.0f) > 0.001f ||
        std::fabs(tooltip.margin - 4.0f) > 0.001f ||
        std::fabs(tooltip.shadowOffsetX - 1.0f) > 0.001f ||
        std::fabs(tooltip.shadowOffsetY + 1.0f) > 0.001f) {
        return fail("tooltip style should map theme colors and preserve layout metrics");
    }

    std::cout << "[ui_style_resolver_test] PASS\n";
    return EXIT_SUCCESS;
}
