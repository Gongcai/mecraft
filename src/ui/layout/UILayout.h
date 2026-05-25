#pragma once

enum class Anchor {
    TopLeft, TopCenter, TopRight,
    CenterLeft, Center, CenterRight,
    BottomLeft, BottomCenter, BottomRight
};

struct UILayout {
    Anchor anchor = Anchor::BottomCenter;
    float offsetX = 0.0f;
    float offsetY = 0.0f;

    // Resolves to bottom-left-origin pixel position (OpenGL convention).
    // screenW/screenH = viewport dimensions.
    // controlW/controlH = size of the control being placed.
    [[nodiscard]] float resolveX(float screenW, float controlW) const;
    [[nodiscard]] float resolveY(float screenH, float controlH) const;
};

// Shared constants for the hotbar widget, used by HotbarControl and HudControl.
namespace HotbarLayout {
    constexpr float kWidgetsWidth = 182.0f;
    constexpr float kWidgetsHeight = 46.0f;
    constexpr float kBgHeight = 21.0f;
    constexpr float kHighlightSize = 25.0f;
    constexpr float kScale = 2.0f;
    constexpr float kBottomMargin = 8.0f;

    constexpr float kWidth = kWidgetsWidth * kScale;    // 364
    constexpr float kHeight = kBgHeight * kScale;       // 42
}
