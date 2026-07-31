#include "UIPanel.h"

#include <algorithm>
#include <cmath>

#include <glm/vec4.hpp>

#include "../../renderer/rhi/RhiCommandList.h"
#include "../../resource/ResourceMgr.h"

namespace {

struct PanelSolidPushConstants {
    glm::vec4 screenRect;
    glm::vec4 rectRadius;
    glm::vec4 color;
};

struct PanelGlassPushConstants {
    glm::vec4 screenRect;
    glm::vec4 extentOpacity;
    glm::vec4 tint;
    glm::vec4 appearance;
};

static_assert(sizeof(PanelSolidPushConstants) == 48u);
static_assert(sizeof(PanelGlassPushConstants) == 64u);

[[nodiscard]] RhiRect2D panelScissor(const UIRenderContext& context) {
    if (context.hasScissor) {
        return context.scissor;
    }
    return {0, 0,
            static_cast<uint32_t>(
                std::max(1.0f, std::round(static_cast<float>(context.screenWidth) * context.pixelScale()))),
            static_cast<uint32_t>(
                std::max(1.0f, std::round(static_cast<float>(context.screenHeight) * context.pixelScale())))};
}

} // namespace

UIPanel::UIPanel() = default;

UIPanel::~UIPanel() {
    shutdown();
}

void UIPanel::init(ResourceMgr& resourceMgr) {
    UIWidget::init(resourceMgr);
}

void UIPanel::shutdown() {
    UIWidget::shutdown();
}

void UIPanel::setTone(const UIPanelTone tone) {
    m_tone = tone;
    clearLocalColors();
}

void UIPanel::renderSelf(const UIRenderContext& context) const {
    if (context.commandList == nullptr || !context.panelQuadVertexBuffer.isValid() ||
        !context.panelSolidPipeline.isValid()) {
        return;
    }

    const float panelX = getAbsoluteX(context);
    const float panelY = getAbsoluteY(context);
    const float panelWidth = width * scaleX;
    const float panelHeight = height * scaleY;
    if (panelWidth <= 0.0f || panelHeight <= 0.0f) {
        return;
    }

    const UIResolvedStyle resolved = UIStyleResolver::resolve(resolveBaseStyle(context), UIStyleState_Normal);
    const bool useGlass =
        context.panelGlassPipeline.isValid() && context.panelGlassBindGroup.isValid() &&
        context.backdropBlurView.isValid() && context.backdropSourceWidth > 0 && context.backdropSourceHeight > 0 &&
        context.backdropBlurWidth > 0 && context.backdropBlurHeight > 0 &&
        (m_tone == UIPanelTone::OverlaySurface || (m_tone == UIPanelTone::Default && !m_hasLocalBgColor));

    RhiCommandList& commandList = *context.commandList;
    const RhiRect2D scissor = panelScissor(context);

    if (useGlass) {
        std::array<float, 4> tint = resolved.background;
        const float tintStrength = std::clamp(tint[3] * 0.42f, 0.18f, 0.42f);
        const float opacity = std::clamp(alpha * 0.94f, 0.0f, 1.0f);
        const PanelGlassPushConstants pushConstants{glm::vec4(static_cast<float>(context.screenWidth),
                                                              static_cast<float>(context.screenHeight), panelX, panelY),
                                                    glm::vec4(panelWidth, panelHeight, 0.0f, opacity),
                                                    glm::vec4(tint[0], tint[1], tint[2], tintStrength),
                                                    glm::vec4(0.58f, 0.74f, 0.0f, 0.0f)};

        commandList.setGraphicsPipeline(context.panelGlassPipeline);
        commandList.setVertexBuffer(0u, context.panelQuadVertexBuffer, 0u);
        commandList.setBindGroup(0u, context.panelGlassBindGroup);
        commandList.setScissor(scissor);
        commandList.pushConstants(&pushConstants, sizeof(pushConstants),
                                  rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment));
        commandList.draw(6u, 1u, 0u, 0u);
    }

    commandList.setGraphicsPipeline(context.panelSolidPipeline);
    commandList.setVertexBuffer(0u, context.panelQuadVertexBuffer, 0u);
    commandList.setScissor(scissor);

    auto drawSolidRect = [&](const float x, const float y, const float rectWidth, const float rectHeight,
                             const std::array<float, 4>& rectColor) {
        if (rectWidth <= 0.0f || rectHeight <= 0.0f || rectColor[3] <= 0.0f) {
            return;
        }
        const PanelSolidPushConstants pushConstants{
            glm::vec4(static_cast<float>(context.screenWidth), static_cast<float>(context.screenHeight), x, y),
            glm::vec4(rectWidth, rectHeight, 0.0f, 0.0f),
            glm::vec4(rectColor[0], rectColor[1], rectColor[2], rectColor[3])};
        commandList.pushConstants(&pushConstants, sizeof(pushConstants),
                                  rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment));
        commandList.draw(6u, 1u, 0u, 0u);
    };

    std::array<float, 4> background = resolved.background;
    background[3] *= alpha * (useGlass ? 0.34f : 1.0f);
    drawSolidRect(panelX, panelY, panelWidth, panelHeight, background);

    const float borderWidth = std::clamp(resolved.borderWidth, 0.0f, std::min(panelWidth, panelHeight) * 0.5f);
    if (borderWidth > 0.0f) {
        std::array<float, 4> border = resolved.border;
        border[3] *= alpha;
        drawSolidRect(panelX, panelY + panelHeight - borderWidth, panelWidth, borderWidth, border);
        drawSolidRect(panelX, panelY, panelWidth, borderWidth, border);
        drawSolidRect(panelX, panelY + borderWidth, borderWidth, panelHeight - borderWidth * 2.0f, border);
        drawSolidRect(panelX + panelWidth - borderWidth, panelY + borderWidth, borderWidth,
                      panelHeight - borderWidth * 2.0f, border);
    }

    if (useGlass) {
        const float lineWidth = std::max(1.0f, borderWidth);
        const float lineSpan = panelWidth - lineWidth * 2.0f;
        const Color topHighlight{0.92f, 0.96f, 0.84f, 0.14f * alpha};
        drawSolidRect(panelX + lineWidth, panelY + panelHeight - lineWidth - 1.0f, lineSpan, 1.0f, topHighlight);

        const Color bottomShade{0.0f, 0.0f, 0.0f, 0.18f * alpha};
        drawSolidRect(panelX + lineWidth, panelY + lineWidth, lineSpan, 1.0f, bottomShade);
    }
}

UIComponentStyle UIPanel::resolveBaseStyle(const UIRenderContext& context) const {
    UIComponentStyle style = UIStyleResolver::panelStyleFromTheme(context.theme, m_tone);

    if (m_hasLocalBgColor) {
        style.backgroundNormal = m_bgColor;
        style.backgroundHover = m_bgColor;
        style.backgroundPressed = m_bgColor;
        style.backgroundDisabled = m_bgColor;
    }

    if (m_hasLocalBorderColor) {
        style.borderNormal = m_borderColor;
        style.borderHover = m_borderColor;
        style.borderFocused = m_borderColor;
        style.borderPressed = m_borderColor;
        style.borderDisabled = m_borderColor;
    }

    if (m_hasLocalBorderWidth) {
        style.borderWidth = m_borderWidth;
    } else if (m_tone == UIPanelTone::Default) {
        style.borderWidth = (m_borderWidth > 0.0f) ? style.borderWidth : 0.0f;
    }

    return style;
}
