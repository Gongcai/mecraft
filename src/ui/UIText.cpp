#include "UIText.h"

#include "TextRenderer.h"
#include "UITheme.h"

float UIText::measureTextWidth(const TextRenderer& tr) const
{
    if (m_text.empty()) return 0.0f;
    return tr.measureText(m_text, m_textScale).width;
}

float UIText::measureTextHeight(const TextRenderer& tr) const
{
    return tr.measureText(m_text, m_textScale).height;
}

void UIText::renderSelf(const UIRenderContext& ctx) const {
    if (m_text.empty() || !ctx.textRenderer) return;

    const UITheme* theme = ctx.theme;

    float ax = getAbsoluteX(ctx);
    float ay = getAbsoluteY(ctx);

    // Apply text alignment
    if (m_alignment != TextAlignment::Left) {
        const float tw = measureTextWidth(*ctx.textRenderer);
        if (m_alignment == TextAlignment::Center) {
            ax += (width * scaleX - tw) * 0.5f;
        } else if (m_alignment == TextAlignment::Right) {
            ax += width * scaleX - tw;
        }
    }

    std::array<float, 4> col = (m_hasLocalTextColor || !theme) ? m_textColor : theme->textPrimary;
    col[3] *= alpha;

    if (m_shadowEnabled) {
        std::array<float, 4> shadow = m_shadowColor;
        shadow[3] *= alpha;
        ctx.textRenderer->render(m_text,
                                 ax + m_shadowOffsetX,
                                 ay + m_shadowOffsetY,
                                 m_textScale,
                                 shadow,
                                 static_cast<float>(ctx.screenWidth),
                                 static_cast<float>(ctx.screenHeight));
    }

    ctx.textRenderer->render(m_text, ax, ay, m_textScale, col,
                             static_cast<float>(ctx.screenWidth),
                             static_cast<float>(ctx.screenHeight));
}
