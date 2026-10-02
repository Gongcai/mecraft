#pragma once

enum class Anchor {
    TopLeft,
    TopCenter,
    TopRight,
    CenterLeft,
    Center,
    CenterRight,
    BottomLeft,
    BottomCenter,
    BottomRight
};

struct UILayout {
    Anchor anchor = Anchor::BottomCenter;
    float offsetX = 0.0f;
    float offsetY = 0.0f;

    /// Resolves the horizontal position in bottom-left-origin UI reference units.
    /// @param uiWidth UI reference width.
    /// @param controlWidth Control width in UI reference units.
    /// @return Horizontal position in UI reference units.
    [[nodiscard]] float resolveX(float uiWidth, float controlWidth) const;

    /// Resolves the vertical position in bottom-left-origin UI reference units.
    /// @param uiHeight UI reference height.
    /// @param controlHeight Control height in UI reference units.
    /// @return Vertical position in UI reference units.
    [[nodiscard]] float resolveY(float uiHeight, float controlHeight) const;
};

// Shared constants for the hotbar widget, used by HotbarControl and HudControl.
namespace HotbarLayout {
constexpr float kWidgetsWidth = 182.0f;
constexpr float kWidgetsHeight = 22.0f;
constexpr float kBgHeight = 22.0f;
constexpr float kHighlightSize = 24.0f;
constexpr float kScale = 2.0f;
constexpr float kBottomMargin = 8.0f;

constexpr float kWidth = kWidgetsWidth * kScale; // 364
constexpr float kHeight = kBgHeight * kScale; // 44
} // namespace HotbarLayout
