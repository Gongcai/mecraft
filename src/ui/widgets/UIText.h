#pragma once

#include <array>
#include <string>

#include "../core/UIWidget.h"

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

    void setTextColor(const std::array<float, 4>& c) { m_textColor = c; m_hasLocalTextColor = true; }
    [[nodiscard]] const std::array<float, 4>& getTextColor() const { return m_textColor; }
    void clearLocalTextColor() { m_hasLocalTextColor = false; }
    [[nodiscard]] bool hasLocalTextColor() const { return m_hasLocalTextColor; }

    void setShadowEnabled(bool enabled) { m_shadowEnabled = enabled; }
    [[nodiscard]] bool isShadowEnabled() const { return m_shadowEnabled; }
    void setShadowColor(const std::array<float, 4>& c) { m_shadowColor = c; }
    void setShadowOffset(float x, float y) { m_shadowOffsetX = x; m_shadowOffsetY = y; }

    void setAlignment(TextAlignment alignment) { m_alignment = alignment; }
    [[nodiscard]] TextAlignment getAlignment() const { return m_alignment; }

    // Measure text dimensions in pixels (at current scale)
    [[nodiscard]] float measureTextWidth(const TextRenderer& tr) const;
    [[nodiscard]] float measureTextHeight(const TextRenderer& tr) const;

protected:
    void renderSelf(const UIRenderContext& ctx) const override;

private:
    std::string m_text;
    float m_textScale = 1.0f;
    std::array<float, 4> m_textColor{1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 4> m_shadowColor{0.0f, 0.0f, 0.0f, 0.65f};
    float m_shadowOffsetX = 1.0f;
    float m_shadowOffsetY = -1.0f;
    TextAlignment m_alignment = TextAlignment::Left;
    bool m_hasLocalTextColor = false;
    bool m_shadowEnabled = false;
};
