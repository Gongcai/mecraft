#include "UIText.h"

#include "TextRenderer.h"

void UIText::renderSelf(const UIRenderContext& ctx) const {
    if (m_text.empty() || !ctx.textRenderer) return;

    float ax = getAbsoluteX(ctx);
    float ay = getAbsoluteY(ctx);

    std::array<float, 4> col = m_textColor;
    col[3] *= alpha;

    ctx.textRenderer->render(m_text, ax, ay, m_textScale, col,
                             static_cast<float>(ctx.screenWidth),
                             static_cast<float>(ctx.screenHeight));
}
