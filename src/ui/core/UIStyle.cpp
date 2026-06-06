#include "UIStyle.h"

namespace UIStyleResolver {

UIComponentStyle panelStyleFromTheme(const UITheme* theme) {
    UIComponentStyle style;
    style.backgroundNormal = {0.2f, 0.2f, 0.2f, 0.8f};
    style.backgroundHover = style.backgroundNormal;
    style.backgroundPressed = style.backgroundNormal;
    style.backgroundDisabled = style.backgroundNormal;
    style.borderNormal = {1.0f, 1.0f, 1.0f, 0.5f};
    style.borderHover = style.borderNormal;
    style.borderFocused = style.borderNormal;
    style.borderPressed = style.borderNormal;
    style.borderDisabled = style.borderNormal;
    style.borderWidth = 0.0f;

    if (!theme) {
        return style;
    }

    style.backgroundNormal = theme->panelBackground;
    style.backgroundHover = theme->panelBackground;
    style.backgroundPressed = theme->panelBackground;
    style.backgroundDisabled = theme->panelBackground;
    style.borderNormal = theme->panelBorder;
    style.borderHover = theme->panelBorder;
    style.borderFocused = theme->panelBorder;
    style.borderPressed = theme->panelBorder;
    style.borderDisabled = theme->panelBorder;
    style.borderWidth = theme->panelBorderWidth;
    return style;
}

UIComponentStyle buttonStyleFromTheme(const UITheme* theme) {
    UIComponentStyle style;
    if (!theme) {
        return style;
    }

    style.backgroundNormal = theme->buttonNormal;
    style.backgroundHover = theme->buttonHover;
    style.backgroundPressed = theme->buttonPressed;
    style.backgroundDisabled = theme->buttonDisabled;

    style.borderNormal = theme->buttonBorder;
    style.borderHover = theme->buttonBorder;
    style.borderFocused = theme->inputBorderFocused;
    style.borderPressed = theme->buttonBorder;
    style.borderDisabled = theme->buttonBorder;

    style.textNormal = theme->textPrimary;
    style.textDisabled = theme->textDisabled;
    style.borderWidth = theme->buttonBorderWidth;
    return style;
}

UITextInputStyle textInputStyleFromTheme(const UITheme* theme) {
    UITextInputStyle style;
    style.frame.backgroundNormal = {0.15f, 0.15f, 0.15f, 0.9f};
    style.frame.backgroundHover = style.frame.backgroundNormal;
    style.frame.backgroundPressed = style.frame.backgroundNormal;
    style.frame.backgroundDisabled = {0.12f, 0.12f, 0.12f, 0.55f};
    style.frame.borderNormal = {0.40f, 0.40f, 0.40f, 0.7f};
    style.frame.borderHover = style.frame.borderNormal;
    style.frame.borderFocused = {0.2f, 0.8f, 1.0f, 1.0f};
    style.frame.borderPressed = style.frame.borderFocused;
    style.frame.borderDisabled = {0.30f, 0.30f, 0.30f, 0.35f};
    style.frame.textNormal = {1.0f, 1.0f, 1.0f, 1.0f};
    style.frame.textDisabled = {0.45f, 0.45f, 0.45f, 1.0f};
    style.frame.borderWidth = 1.0f;

    if (!theme) {
        return style;
    }

    style.frame.backgroundNormal = theme->inputBackground;
    style.frame.backgroundHover = theme->inputBackground;
    style.frame.backgroundPressed = theme->inputBackground;
    style.frame.backgroundDisabled = theme->buttonDisabled;
    style.frame.borderNormal = theme->inputBorder;
    style.frame.borderHover = theme->inputBorder;
    style.frame.borderFocused = theme->inputBorderFocused;
    style.frame.borderPressed = theme->inputBorderFocused;
    style.frame.borderDisabled = theme->buttonBorder;
    style.frame.textNormal = theme->inputText;
    style.frame.textDisabled = theme->textDisabled;
    style.frame.borderWidth = theme->panelBorderWidth;
    style.placeholder = theme->inputPlaceholder;
    style.selection = theme->inputSelection;
    style.cursor = theme->inputCursor;
    return style;
}

UIToggleStyle toggleStyleFromTheme(const UITheme* theme) {
    UIToggleStyle style;
    if (!theme) {
        return style;
    }

    style.trackOff = theme->toggleTrackOff;
    style.trackOn = theme->toggleTrackOn;
    style.trackDisabled = theme->buttonDisabled;
    style.knobNormal = theme->toggleKnob;
    style.knobHover = theme->toggleKnobHover;
    style.knobDisabled = theme->textDisabled;
    style.textNormal = theme->textPrimary;
    style.textDisabled = theme->textDisabled;
    style.width = theme->toggleWidth;
    style.height = theme->toggleHeight;
    return style;
}

UICheckboxStyle checkboxStyleFromTheme(const UITheme* theme) {
    UICheckboxStyle style;
    if (!theme) {
        return style;
    }

    style.boxNormal = theme->checkboxBox;
    style.boxHover = theme->checkboxBoxHover;
    style.boxDisabled = theme->buttonDisabled;
    style.borderNormal = theme->checkboxBoxBorder;
    style.borderDisabled = theme->buttonBorder;
    style.check = theme->checkboxCheck;
    style.textNormal = theme->textPrimary;
    style.textDisabled = theme->textDisabled;
    style.boxSize = theme->checkboxSize;
    return style;
}

UISliderStyle sliderStyleFromTheme(const UITheme* theme) {
    UISliderStyle style;
    if (!theme) {
        return style;
    }

    style.trackNormal = theme->sliderTrack;
    style.trackDisabled = theme->buttonDisabled;
    style.fillNormal = theme->sliderFill;
    style.fillDisabled = theme->buttonBorder;
    style.handleNormal = theme->sliderHandle;
    style.handleHover = theme->sliderHandleHover;
    style.handleDisabled = theme->textDisabled;
    style.trackHeight = theme->sliderTrackHeight;
    style.handleSize = theme->sliderHandleSize;
    return style;
}

UIProgressBarStyle progressBarStyleFromTheme(const UITheme* theme) {
    UIProgressBarStyle style;
    if (!theme) {
        return style;
    }

    style.track = theme->progressTrack;
    style.fill = theme->progressFill;
    style.text = theme->progressText;
    return style;
}

UIResolvedStyle resolve(const UIComponentStyle& style, int state) {
    UIResolvedStyle resolved;
    resolved.borderWidth = style.borderWidth;

    if (hasStyleState(state, UIStyleState_Disabled)) {
        resolved.background = style.backgroundDisabled;
        resolved.border = style.borderDisabled;
        resolved.text = style.textDisabled;
        return resolved;
    }

    if (hasStyleState(state, UIStyleState_Pressed)) {
        resolved.background = style.backgroundPressed;
        resolved.border = style.borderPressed;
    } else if (hasStyleState(state, UIStyleState_Hovered)) {
        resolved.background = style.backgroundHover;
        resolved.border = style.borderHover;
    } else {
        resolved.background = style.backgroundNormal;
        resolved.border = style.borderNormal;
    }

    if (hasStyleState(state, UIStyleState_Focused)) {
        resolved.border = style.borderFocused;
    }

    resolved.text = style.textNormal;
    return resolved;
}

UIResolvedSliderStyle resolveSlider(const UISliderStyle& style, int state) {
    UIResolvedSliderStyle resolved;
    resolved.trackHeight = style.trackHeight;
    resolved.handleSize = style.handleSize;

    if (hasStyleState(state, UIStyleState_Disabled)) {
        resolved.track = style.trackDisabled;
        resolved.fill = style.fillDisabled;
        resolved.handle = style.handleDisabled;
        return resolved;
    }

    resolved.track = style.trackNormal;
    resolved.fill = style.fillNormal;
    resolved.handle = hasStyleState(state, UIStyleState_Hovered) ? style.handleHover : style.handleNormal;
    return resolved;
}

UIResolvedProgressBarStyle resolveProgressBar(const UIProgressBarStyle& style) {
    UIResolvedProgressBarStyle resolved;
    resolved.track = style.track;
    resolved.fill = style.fill;
    resolved.text = style.text;
    return resolved;
}

UIResolvedTextInputStyle resolveTextInput(const UITextInputStyle& style, int state) {
    UIResolvedTextInputStyle resolved;
    resolved.frame = resolve(style.frame, state);

    if (hasStyleState(state, UIStyleState_Disabled)) {
        resolved.placeholder = style.frame.textDisabled;
        resolved.selection = {style.selection[0], style.selection[1], style.selection[2], style.selection[3] * 0.45f};
        resolved.cursor = {style.cursor[0], style.cursor[1], style.cursor[2], style.cursor[3] * 0.45f};
        return resolved;
    }

    resolved.placeholder = style.placeholder;
    resolved.selection = style.selection;
    resolved.cursor = style.cursor;
    return resolved;
}

UIResolvedToggleStyle resolveToggle(const UIToggleStyle& style, int state) {
    UIResolvedToggleStyle resolved;
    resolved.width = style.width;
    resolved.height = style.height;

    if (hasStyleState(state, UIStyleState_Disabled)) {
        resolved.trackOff = style.trackDisabled;
        resolved.trackOn = style.trackDisabled;
        resolved.knob = style.knobDisabled;
        resolved.text = style.textDisabled;
        return resolved;
    }

    resolved.trackOff = style.trackOff;
    resolved.trackOn = style.trackOn;
    resolved.knob = hasStyleState(state, UIStyleState_Hovered) ? style.knobHover : style.knobNormal;
    resolved.text = style.textNormal;
    return resolved;
}

UIResolvedCheckboxStyle resolveCheckbox(const UICheckboxStyle& style, int state) {
    UIResolvedCheckboxStyle resolved;
    resolved.boxSize = style.boxSize;
    resolved.borderWidth = style.borderWidth;

    if (hasStyleState(state, UIStyleState_Disabled)) {
        resolved.box = style.boxDisabled;
        resolved.border = style.borderDisabled;
        resolved.check = style.textDisabled;
        resolved.text = style.textDisabled;
        return resolved;
    }

    resolved.box = hasStyleState(state, UIStyleState_Hovered) ? style.boxHover : style.boxNormal;
    resolved.border = style.borderNormal;
    resolved.check = style.check;
    resolved.text = style.textNormal;
    return resolved;
}

} // namespace UIStyleResolver
