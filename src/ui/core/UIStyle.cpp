#include "UIStyle.h"

namespace UIStyleResolver {

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

} // namespace UIStyleResolver
