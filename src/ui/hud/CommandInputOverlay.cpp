#include "CommandInputOverlay.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <glm/vec4.hpp>

#include "../font/TextRenderer.h"
#include "../core/UIStyle.h"
#include "../../renderer/rhi/RhiCommandList.h"

namespace {

struct PanelSolidPushConstants {
    glm::vec4 screenRect;
    glm::vec4 rectRadius;
    glm::vec4 color;
};

static_assert(sizeof(PanelSolidPushConstants) == 48u);

[[nodiscard]] RhiRect2D fullOverlayScissor(const UIRenderContext& context) {
    return context.fullFramebufferScissor();
}

[[nodiscard]] RhiRect2D physicalOverlayScissor(const UIRenderContext& context, const int x, const int y,
                                               const int width, const int height) {
    return context.uiRectToFramebufferScissor(static_cast<float>(x), static_cast<float>(y),
                                              static_cast<float>(width), static_cast<float>(height));
}

} // namespace

void CommandInputOverlay::init(GameResources& resources, RhiDevice& rhiDevice) {
    UIWidget::init(resources, rhiDevice);
}

void CommandInputOverlay::shutdown() {
    m_text.clear();
    UIWidget::shutdown();
}

void CommandInputOverlay::setText(std::string text) {
    m_text = std::move(text);
}

const std::string& CommandInputOverlay::getText() const {
    return m_text;
}

void CommandInputOverlay::renderSelf(const UIRenderContext& context) const {
    if (context.textRenderer == nullptr || context.commandInputText == nullptr) {
        return;
    }

    renderBox(*context.commandInputText, *context.textRenderer, context);
}

void CommandInputOverlay::setCaretBlinkPeriodMs(float periodMs) {
    m_caretBlinkPeriodMs = std::clamp(periodMs, 120.0f, 2500.0f);
}

float CommandInputOverlay::getCaretBlinkPeriodMs() const {
    return m_caretBlinkPeriodMs;
}

CommandInputOverlay::ClipInfo CommandInputOverlay::computeClipInfo(const std::string& text, int boxX, int boxY,
                                                                   int boxW, int boxH, float textScale,
                                                                   int textPaddingX, int textPaddingY,
                                                                   const TextRenderer& textRenderer) {
    ClipInfo info;
    info.glyphSize = 8.0f * textScale;
    info.clipX = boxX + textPaddingX;
    info.clipY = boxY + textPaddingY;
    info.clipW = std::max(1, boxW - textPaddingX * 2);
    info.clipH = std::max(1, boxH - textPaddingY * 2);

    const float clipContentW = static_cast<float>(info.clipW) - 4.0f;

    // Find how many trailing characters fit in the clip width
    size_t maxVisibleChars = 0;
    float accumulatedW = 0.0f;
    for (size_t i = text.size(); i > 0; --i) {
        const float w = textRenderer.measureText(text.substr(i - 1, 1), textScale).width;
        if (accumulatedW + w > clipContentW)
            break;
        accumulatedW += w;
        maxVisibleChars++;
    }
    maxVisibleChars = std::max<size_t>(1, maxVisibleChars);

    const size_t visibleStart = (text.size() > maxVisibleChars) ? (text.size() - maxVisibleChars) : 0;
    info.visibleText = text.substr(visibleStart);

    info.textX = static_cast<float>(info.clipX + 2);
    info.textY = static_cast<float>(info.clipY) + (static_cast<float>(info.clipH) - info.glyphSize) * 0.5f;
    return info;
}

bool CommandInputOverlay::isCaretVisible(double nowSec, float blinkPeriodMs) {
    const double blinkPhaseMs = std::fmod(nowSec * 1000.0, static_cast<double>(blinkPeriodMs));
    return blinkPhaseMs < static_cast<double>(blinkPeriodMs * 0.5f);
}

CommandInputOverlay::CaretRect
CommandInputOverlay::computeCaretRect(const ClipInfo& info, const TextRenderer& textRenderer, float textScale) {
    const float caretX = info.textX + textRenderer.measureText(info.visibleText, textScale).width;
    CaretRect rect;
    rect.x = static_cast<int>(std::round(std::min(caretX, static_cast<float>(info.clipX + info.clipW - 3))));
    rect.y = static_cast<int>(std::floor(info.textY));
    rect.h = std::max(1, static_cast<int>(std::round(info.glyphSize)));
    return rect;
}

void CommandInputOverlay::drawOverlayRect(const UIRenderContext& context, int rectX, int rectY, int rectW, int rectH,
                                          const std::array<float, 4>& rectColor) const {
    if (context.commandList == nullptr || !context.panelQuadVertexBuffer.isValid() ||
        !context.panelSolidPipeline.isValid() || rectW <= 0 || rectH <= 0 || rectColor[3] <= 0.0f) {
        return;
    }

    const PanelSolidPushConstants pushConstants{
        glm::vec4(static_cast<float>(context.uiWidth), static_cast<float>(context.uiHeight),
                  static_cast<float>(rectX), static_cast<float>(rectY)),
        glm::vec4(static_cast<float>(rectW), static_cast<float>(rectH), 0.0f, 0.0f),
        glm::vec4(rectColor[0], rectColor[1], rectColor[2], rectColor[3])};

    RhiCommandList& commandList = *context.commandList;
    commandList.setGraphicsPipeline(context.panelSolidPipeline);
    commandList.setVertexBuffer(0u, context.panelQuadVertexBuffer, 0u);
    commandList.pushConstants(&pushConstants, sizeof(pushConstants),
                              rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment));
    commandList.draw(6u, 1u, 0u, 0u);
}

void CommandInputOverlay::renderBox(const std::string& text, const TextRenderer& textRenderer,
                                    const UIRenderContext& context) const {
    const bool record = context.phase == UIRenderPhase::Record;
    if (record && (context.commandList == nullptr || !context.panelQuadVertexBuffer.isValid() ||
                   !context.panelSolidPipeline.isValid())) {
        return;
    }

    const int screenW = context.uiWidth;
    const int screenH = context.uiHeight;
    if (screenW <= 0 || screenH <= 0) {
        return;
    }

    const UIResolvedConsoleStyle style =
        UIStyleResolver::resolveConsole(UIStyleResolver::consoleStyleFromTheme(context.theme));
    const int boxW = std::max(
        style.minBoxWidth, std::min(screenW - style.horizontalMargin * 2,
                                    static_cast<int>(std::round(static_cast<float>(screenW) * style.boxWidthRatio))));
    const int boxH = style.inputBoxHeight;
    const int boxX = style.x;
    const int boxY = style.inputY;
    const float textScale = style.textScale;
    const auto clipInfo =
        computeClipInfo(text, boxX, boxY, boxW, boxH, textScale, style.textPaddingX, style.textPaddingY, textRenderer);

    const RhiRect2D parentScissor = fullOverlayScissor(context);
    const RhiRect2D contentScissor =
        physicalOverlayScissor(context, clipInfo.clipX, clipInfo.clipY, clipInfo.clipW, clipInfo.clipH);

    if (record) {
        context.commandList->setScissor(parentScissor);
        drawOverlayRect(context, boxX, boxY, boxW, boxH, style.box);
        context.commandList->setScissor(contentScissor);
    }
    UIRenderContext textContext = context;
    textContext.hasScissor = true;
    textContext.scissor = contentScissor;
    textRenderer.draw(textContext, clipInfo.visibleText, clipInfo.textX, clipInfo.textY, textScale, style.textNormal);

    if (record && isCaretVisible(static_cast<double>(context.timeSeconds), m_caretBlinkPeriodMs)) {
        const CaretRect caret = computeCaretRect(clipInfo, textRenderer, textScale);
        context.commandList->setScissor(contentScissor);
        drawOverlayRect(context, caret.x, caret.y, caret.w, caret.h, style.textNormal);
    }

    if (record) {
        context.commandList->setScissor(parentScissor);
    }
}
