#include "UIText.h"

#include "../font/TextRenderer.h"

float UIText::measureTextWidth(const TextRenderer& tr) const
{
    if (m_text.empty()) return 0.0f;
    return tr.measureText(m_text, m_textScale).width;
}

float UIText::measureTextHeight(const TextRenderer& tr) const
{
    return tr.measureText(m_text, m_textScale).height;
}

void UIText::setTone(UITextTone tone) {
    m_tone = tone;
    m_hasLocalTextColor = false;
}

void UIText::renderSelf(const UIRenderContext& ctx) const {
    if (m_text.empty() || !ctx.textRenderer) return;

    const UIResolvedTextStyle resolved = resolveStyle(ctx);

    float ax = getAbsoluteX(ctx);
    float ay = getAbsoluteY(ctx);

    // Apply text alignment
    if (m_alignment != TextAlignment::Left) {
        const float tw = ctx.textRenderer->measureText(m_text, resolved.textScale).width;
        if (m_alignment == TextAlignment::Center) {
            ax += (width * scaleX - tw) * 0.5f;
        } else if (m_alignment == TextAlignment::Right) {
            ax += width * scaleX - tw;
        }
    }

    std::array<float, 4> col = resolved.text;
    col[3] *= alpha;

    if (m_shadowEnabled) {
        std::array<float, 4> shadow = resolved.shadow;
        shadow[3] *= alpha;
        ctx.textRenderer->draw(ctx,
                               m_text,
                               ax + resolved.shadowOffsetX,
                               ay + resolved.shadowOffsetY,
                               resolved.textScale,
                               shadow);
    }

    ctx.textRenderer->draw(ctx, m_text, ax, ay, resolved.textScale, col);
}

UITextStyle UIText::resolveBaseStyle(const UIRenderContext& ctx) const {
    UITextStyle style = UIStyleResolver::textStyleFromTheme(ctx.theme, m_tone);
    style.textScale = m_textScale;
    style.shadow = m_shadowColor;
    style.shadowOffsetX = m_shadowOffsetX;
    style.shadowOffsetY = m_shadowOffsetY;
    if (m_hasLocalTextColor) {
        style.text = m_textColor;
    }
    return style;
}

UIResolvedTextStyle UIText::resolveStyle(const UIRenderContext& ctx) const {
    return UIStyleResolver::resolveText(resolveBaseStyle(ctx));
}
