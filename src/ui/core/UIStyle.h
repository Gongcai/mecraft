#pragma once

#include "UITheme.h"

enum UIStyleState {
    UIStyleState_Normal   = 0,
    UIStyleState_Hovered  = 1 << 0,
    UIStyleState_Pressed  = 1 << 1,
    UIStyleState_Focused  = 1 << 2,
    UIStyleState_Disabled = 1 << 3,
};

[[nodiscard]] inline constexpr bool hasStyleState(int state, UIStyleState flag) {
    return (state & static_cast<int>(flag)) != 0;
}

struct UIComponentStyle {
    Color backgroundNormal   {0.28f, 0.28f, 0.28f, 0.92f};
    Color backgroundHover    {0.42f, 0.42f, 0.42f, 1.0f};
    Color backgroundPressed  {0.20f, 0.20f, 0.20f, 1.0f};
    Color backgroundDisabled {0.22f, 0.22f, 0.22f, 0.5f};

    Color borderNormal       {0.50f, 0.50f, 0.50f, 0.4f};
    Color borderHover        {0.62f, 0.62f, 0.62f, 0.55f};
    Color borderFocused      {0.2f, 0.8f, 1.0f, 0.85f};
    Color borderPressed      {0.70f, 0.70f, 0.70f, 0.45f};
    Color borderDisabled     {0.35f, 0.35f, 0.35f, 0.25f};

    Color textNormal         {1.0f, 1.0f, 1.0f, 1.0f};
    Color textDisabled       {0.45f, 0.45f, 0.45f, 1.0f};

    float borderWidth = 2.0f;
};

struct UIResolvedStyle {
    Color background {0.28f, 0.28f, 0.28f, 0.92f};
    Color border     {0.50f, 0.50f, 0.50f, 0.4f};
    Color text       {1.0f, 1.0f, 1.0f, 1.0f};
    float borderWidth = 2.0f;
};

struct UITextInputStyle {
    UIComponentStyle frame;
    Color placeholder {0.5f, 0.5f, 0.5f, 0.8f};
    Color selection   {0.2f, 0.5f, 0.9f, 0.4f};
    Color cursor      {1.0f, 1.0f, 1.0f, 0.9f};
};

struct UIResolvedTextInputStyle {
    UIResolvedStyle frame;
    Color placeholder {0.5f, 0.5f, 0.5f, 0.8f};
    Color selection   {0.2f, 0.5f, 0.9f, 0.4f};
    Color cursor      {1.0f, 1.0f, 1.0f, 0.9f};
};

namespace UIStyleResolver {
    [[nodiscard]] UIComponentStyle panelStyleFromTheme(const UITheme* theme);
    [[nodiscard]] UIComponentStyle buttonStyleFromTheme(const UITheme* theme);
    [[nodiscard]] UITextInputStyle textInputStyleFromTheme(const UITheme* theme);
    [[nodiscard]] UIResolvedStyle resolve(const UIComponentStyle& style, int state);
    [[nodiscard]] UIResolvedTextInputStyle resolveTextInput(const UITextInputStyle& style, int state);
}
