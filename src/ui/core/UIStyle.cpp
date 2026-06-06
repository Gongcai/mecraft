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

} // namespace UIStyleResolver
