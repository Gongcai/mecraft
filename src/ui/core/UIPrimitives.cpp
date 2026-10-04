#include "UIPrimitives.h"

#include <algorithm>

#include <glm/vec4.hpp>

#include "UIRenderContext.h"
#include "../font/TextRenderer.h"
#include "../widgets/UIPanel.h"
#include "renderer/rhi/RhiCommandList.h"

namespace {
struct SolidRectPushConstants {
    glm::vec4 screenRect;
    glm::vec4 rectRadius;
    glm::vec4 color;
};

static_assert(sizeof(SolidRectPushConstants) == 48u);
} // namespace

void ui::drawSolidRect(const UIRenderContext& context, const float x, const float y, const float width,
                       const float height, const std::array<float, 4>& color) {
    if (context.phase != UIRenderPhase::Record || context.commandList == nullptr ||
        !context.panelQuadVertexBuffer.isValid() || !context.panelSolidPipeline.isValid() || width <= 0.0f ||
        height <= 0.0f || color[3] <= 0.0f) {
        return;
    }

    const SolidRectPushConstants pushConstants{
        glm::vec4(static_cast<float>(context.uiWidth), static_cast<float>(context.uiHeight), x, y),
        glm::vec4(width, height, 0.0f, 0.0f), glm::vec4(color[0], color[1], color[2], color[3])};
    context.commandList->setGraphicsPipeline(context.panelSolidPipeline);
    context.commandList->setVertexBuffer(0u, context.panelQuadVertexBuffer, 0u);
    context.commandList->setScissor(context.fullFramebufferScissor());
    context.commandList->pushConstants(&pushConstants, sizeof(pushConstants),
                                       rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment));
    context.commandList->draw(6u, 1u, 0u, 0u);
}

void ui::drawFramedRect(const UIRenderContext& context, const float x, const float y, const float width,
                        const float height, const std::array<float, 4>& background, const std::array<float, 4>& border,
                        const float borderWidth) {
    if (width <= 0.0f || height <= 0.0f) {
        return;
    }

    const float edge = std::clamp(borderWidth, 0.0f, std::min(width, height) * 0.5f);
    drawSolidRect(context, x, y, width, height, background);
    if (edge <= 0.0f) {
        return;
    }
    drawSolidRect(context, x, y, width, edge, border);
    drawSolidRect(context, x, y + height - edge, width, edge, border);
    drawSolidRect(context, x, y + edge, edge, height - edge * 2.0f, border);
    drawSolidRect(context, x + width - edge, y + edge, edge, height - edge * 2.0f, border);
}

void ui::drawText(const UIRenderContext& context, const std::string& text, const float x, const float y,
                  const float scale, const std::array<float, 4>& color) {
    if (context.textRenderer != nullptr && !text.empty() && scale > 0.0f) {
        context.textRenderer->draw(context, text, x, y, scale, color);
    }
}

void ui::drawPanelSurface(const UIRenderContext& context, const float x, const float y, const float width,
                          const float height) {
    UIPanel panel;
    panel.anchor = Anchor::BottomLeft;
    panel.x = x;
    panel.y = y;
    panel.width = width;
    panel.height = height;
    panel.setTone(UIPanelTone::OverlaySurface);
    panel.render(context);
}

void ui::drawArrow(const UIRenderContext& context, const float x, const float y, const float size,
                   const std::array<float, 4>& color) {
    const float stroke = size / 6.0f;
    drawSolidRect(context, x, y + stroke * 2.0f, size, stroke, color);
    for (int i = 0; i < 3; ++i) {
        drawSolidRect(context, x + size - stroke * static_cast<float>(i + 1), y + stroke * static_cast<float>(2 - i),
                      stroke, stroke * static_cast<float>(1 + i * 2), color);
    }
}
