#pragma once

#include <array>
#include <string>

#include "UIWidget.h"

class TextRenderer;

enum class TextAlignment {
    Left,
    Center,
    Right,
};

class UIText : public UIWidget {
public:
    UIText() = default;

    void setText(const std::string& text) { m_text = text; }
    [[nodiscard]] const std::string& getText() const { return m_text; }

    void setTextScale(float scale) { m_textScale = scale; }
    [[nodiscard]] float getTextScale() const { return m_textScale; }

    void setTextColor(const std::array<float, 4>& c) { m_textColor = c; }
    [[nodiscard]] const std::array<float, 4>& getTextColor() const { return m_textColor; }

    void setAlignment(TextAlignment alignment) { m_alignment = alignment; }
    [[nodiscard]] TextAlignment getAlignment() const { return m_alignment; }

    // Measure text dimensions in pixels (at current scale)
    [[nodiscard]] float measureTextWidth() const {
        // Bitmap font: 8px glyph width, advance factor 0.70
        constexpr float glyphW = 8.0f;
        constexpr float advanceFactor = 0.70f;
        if (m_text.empty()) return 0.0f;
        return static_cast<float>(m_text.size()) * glyphW * m_textScale * advanceFactor
               + glyphW * m_textScale * (1.0f - advanceFactor); // last char full width
    }

    [[nodiscard]] float measureTextHeight() const {
        constexpr float glyphH = 8.0f;
        return glyphH * m_textScale;
    }

protected:
    void renderSelf(const UIRenderContext& ctx) const override;

private:
    std::string m_text;
    float m_textScale = 1.0f;
    std::array<float, 4> m_textColor{1.0f, 1.0f, 1.0f, 1.0f};
    TextAlignment m_alignment = TextAlignment::Left;
};
