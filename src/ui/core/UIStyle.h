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

struct UIToggleStyle {
    Color trackOff       {0.25f, 0.25f, 0.25f, 0.9f};
    Color trackOn        {0.2f, 0.7f, 0.4f, 1.0f};
    Color trackDisabled  {0.18f, 0.18f, 0.18f, 0.55f};
    Color knobNormal     {0.9f, 0.9f, 0.9f, 1.0f};
    Color knobHover      {1.0f, 1.0f, 1.0f, 1.0f};
    Color knobDisabled   {0.55f, 0.55f, 0.55f, 0.8f};
    Color textNormal     {1.0f, 1.0f, 1.0f, 1.0f};
    Color textDisabled   {0.45f, 0.45f, 0.45f, 1.0f};
    float width = 44.0f;
    float height = 22.0f;
};

struct UIResolvedToggleStyle {
    Color trackOff       {0.25f, 0.25f, 0.25f, 0.9f};
    Color trackOn        {0.2f, 0.7f, 0.4f, 1.0f};
    Color knob           {0.9f, 0.9f, 0.9f, 1.0f};
    Color text           {1.0f, 1.0f, 1.0f, 1.0f};
    float width = 44.0f;
    float height = 22.0f;
};

struct UICheckboxStyle {
    Color boxNormal      {0.25f, 0.25f, 0.25f, 0.9f};
    Color boxHover       {0.35f, 0.35f, 0.35f, 1.0f};
    Color boxDisabled    {0.18f, 0.18f, 0.18f, 0.55f};
    Color borderNormal   {0.5f, 0.5f, 0.5f, 0.5f};
    Color borderDisabled {0.35f, 0.35f, 0.35f, 0.3f};
    Color check          {0.3f, 0.8f, 0.4f, 1.0f};
    Color textNormal     {1.0f, 1.0f, 1.0f, 1.0f};
    Color textDisabled   {0.45f, 0.45f, 0.45f, 1.0f};
    float boxSize = 20.0f;
    float borderWidth = 1.0f;
};

struct UIResolvedCheckboxStyle {
    Color box    {0.25f, 0.25f, 0.25f, 0.9f};
    Color border {0.5f, 0.5f, 0.5f, 0.5f};
    Color check  {0.3f, 0.8f, 0.4f, 1.0f};
    Color text   {1.0f, 1.0f, 1.0f, 1.0f};
    float boxSize = 20.0f;
    float borderWidth = 1.0f;
};

struct UISliderStyle {
    Color trackNormal   {0.25f, 0.25f, 0.25f, 1.0f};
    Color trackDisabled {0.18f, 0.18f, 0.18f, 0.55f};
    Color fillNormal    {0.3f, 0.6f, 1.0f, 1.0f};
    Color fillDisabled  {0.30f, 0.30f, 0.30f, 0.55f};
    Color handleNormal  {0.85f, 0.85f, 0.85f, 1.0f};
    Color handleHover   {1.0f, 1.0f, 1.0f, 1.0f};
    Color handleDisabled{0.55f, 0.55f, 0.55f, 0.8f};
    float trackHeight = 4.0f;
    float handleSize = 14.0f;
};

struct UIResolvedSliderStyle {
    Color track  {0.25f, 0.25f, 0.25f, 1.0f};
    Color fill   {0.3f, 0.6f, 1.0f, 1.0f};
    Color handle {0.85f, 0.85f, 0.85f, 1.0f};
    float trackHeight = 4.0f;
    float handleSize = 14.0f;
};

struct UIProgressBarStyle {
    Color track {0.2f, 0.2f, 0.2f, 0.9f};
    Color fill  {0.2f, 0.8f, 1.0f, 1.0f};
    Color text  {1.0f, 1.0f, 1.0f, 1.0f};
};

struct UIResolvedProgressBarStyle {
    Color track {0.2f, 0.2f, 0.2f, 0.9f};
    Color fill  {0.2f, 0.8f, 1.0f, 1.0f};
    Color text  {1.0f, 1.0f, 1.0f, 1.0f};
};

namespace UIStyleResolver {
    [[nodiscard]] UIComponentStyle panelStyleFromTheme(const UITheme* theme);
    [[nodiscard]] UIComponentStyle buttonStyleFromTheme(const UITheme* theme);
    [[nodiscard]] UITextInputStyle textInputStyleFromTheme(const UITheme* theme);
    [[nodiscard]] UIToggleStyle toggleStyleFromTheme(const UITheme* theme);
    [[nodiscard]] UICheckboxStyle checkboxStyleFromTheme(const UITheme* theme);
    [[nodiscard]] UISliderStyle sliderStyleFromTheme(const UITheme* theme);
    [[nodiscard]] UIProgressBarStyle progressBarStyleFromTheme(const UITheme* theme);
    [[nodiscard]] UIResolvedStyle resolve(const UIComponentStyle& style, int state);
    [[nodiscard]] UIResolvedTextInputStyle resolveTextInput(const UITextInputStyle& style, int state);
    [[nodiscard]] UIResolvedToggleStyle resolveToggle(const UIToggleStyle& style, int state);
    [[nodiscard]] UIResolvedCheckboxStyle resolveCheckbox(const UICheckboxStyle& style, int state);
    [[nodiscard]] UIResolvedSliderStyle resolveSlider(const UISliderStyle& style, int state);
    [[nodiscard]] UIResolvedProgressBarStyle resolveProgressBar(const UIProgressBarStyle& style);
}
