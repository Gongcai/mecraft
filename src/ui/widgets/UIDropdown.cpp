#include "UIDropdown.h"

#include <algorithm>
#include <cmath>

#include <glm/vec4.hpp>

#include "../../renderer/rhi/RhiCommandList.h"
#include "../font/TextRenderer.h"

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

[[nodiscard]] RhiRect2D dropdownScissor(const UIRenderContext& context) {
    if (context.hasScissor) {
        return context.scissor;
    }
    return {
        0,
        0,
        static_cast<uint32_t>(std::max(1.0f,
            std::round(static_cast<float>(context.screenWidth) * context.pixelScale()))),
        static_cast<uint32_t>(std::max(1.0f,
            std::round(static_cast<float>(context.screenHeight) * context.pixelScale())))
    };
}

Color scaledColor(Color color, float rgbScale, float alphaScale) {
    color[0] = std::clamp(color[0] * rgbScale, 0.0f, 1.0f);
    color[1] = std::clamp(color[1] * rgbScale, 0.0f, 1.0f);
    color[2] = std::clamp(color[2] * rgbScale, 0.0f, 1.0f);
    color[3] = std::clamp(color[3] * alphaScale, 0.0f, 1.0f);
    return color;
}

Color accentFromSelection(Color color) {
    color[0] = std::clamp(color[0] * 1.35f + 0.08f, 0.0f, 1.0f);
    color[1] = std::clamp(color[1] * 1.35f + 0.08f, 0.0f, 1.0f);
    color[2] = std::clamp(color[2] * 1.35f + 0.08f, 0.0f, 1.0f);
    color[3] = 0.88f;
    return color;
}
} // namespace

UIDropdown::UIDropdown() {
    interactive = true;
    focusable = true;
    width = 200.0f;
    height = 32.0f;
}

UIDropdown::~UIDropdown() { shutdown(); }

void UIDropdown::init(ResourceMgr& resourceMgr) {
    m_expandTween.setImmediate(0.0f);
    m_hoverColorTween.setImmediate({0.30f, 0.30f, 0.30f, 1.0f});
    UIWidget::init(resourceMgr);
}

void UIDropdown::shutdown() {
    UIWidget::shutdown();
}

void UIDropdown::setOptions(std::vector<std::string> options) {
    m_options = std::move(options);
    m_selectedIndex = m_options.empty() ? -1 : std::clamp(m_selectedIndex, -1, static_cast<int>(m_options.size()) - 1);
    m_scrollOffset = 0.0f;
}

void UIDropdown::setSelectedIndex(int index) {
    if (index >= -1 && index < static_cast<int>(m_options.size())) {
        m_selectedIndex = index;
    }
}

int UIDropdown::getSelectedIndex() const {
    return m_selectedIndex;
}

const std::string& UIDropdown::getSelectedText() const {
    static const std::string empty;
    if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_options.size())) {
        return m_options[m_selectedIndex];
    }
    return empty;
}

void UIDropdown::setOnSelectionChanged(std::function<void(int, const std::string&)> callback) {
    m_onSelectionChanged = std::move(callback);
}

void UIDropdown::setStyle(const UIDropdownStyle& style) {
    m_localStyle = style;
    m_hasLocalStyle = true;
}

void UIDropdown::clearLocalStyle() {
    m_hasLocalStyle = false;
}

void UIDropdown::updateAnimations(float dt) {
    m_expandTween.tick(dt);
    m_hoverColorTween.tick(dt);
    UIWidget::updateAnimations(dt);
}

void UIDropdown::renderSelf(const UIRenderContext& ctx) const {
    if (ctx.phase == UIRenderPhase::Record &&
        (ctx.commandList == nullptr ||
         !ctx.panelQuadVertexBuffer.isValid() ||
         !ctx.panelSolidPipeline.isValid())) {
        return;
    }
    renderCollapsed(ctx);
}

void UIDropdown::renderOverlay(const UIRenderContext& ctx) const {
    if (!visible) return;

    if (m_expanded || m_expandTween.isRunning()) {
        renderExpanded(ctx);
    }

    UIWidget::renderOverlay(ctx);
}

UIEventResult UIDropdown::onOverlayInput(const UIInputEvent& event, const UIRenderContext& ctx) {
    if (!visible || !interactive) return UIEventResult::Ignored;
    if (!m_expanded) return UIWidget::onOverlayInput(event, ctx);

    const UIEventResult result = onInput(event, ctx);
    return result == UIEventResult::Ignored ? UIEventResult::Ignored : UIEventResult::Consumed;
}

void UIDropdown::renderCollapsed(const UIRenderContext& ctx) const {
    const bool record = ctx.phase == UIRenderPhase::Record;
    if (record) {
        ctx.commandList->setGraphicsPipeline(ctx.panelSolidPipeline);
        ctx.commandList->setVertexBuffer(0u, ctx.panelQuadVertexBuffer, 0u);
        ctx.commandList->setScissor(dropdownScissor(ctx));
    }

    auto drawSolidRect = [&](const float x,
                             const float y,
                             const float rectWidth,
                             const float rectHeight,
                             const Color& rectColor) {
        if (rectWidth <= 0.0f || rectHeight <= 0.0f || rectColor[3] <= 0.0f) {
            return;
        }
        if (!record) {
            return;
        }
        const PanelSolidPushConstants pushConstants{
            glm::vec4(static_cast<float>(ctx.screenWidth),
                      static_cast<float>(ctx.screenHeight), x, y),
            glm::vec4(rectWidth, rectHeight, 0.0f, 0.0f),
            glm::vec4(rectColor[0], rectColor[1], rectColor[2], rectColor[3])
        };
        ctx.commandList->pushConstants(&pushConstants, sizeof(pushConstants),
                                       rhiFlag(RhiShaderStage::Vertex) |
                                       rhiFlag(RhiShaderStage::Fragment));
        ctx.commandList->draw(6u, 1u, 0u, 0u);
    };

    const UIResolvedDropdownStyle resolved = resolveStyle(ctx);
    Color bgCol = resolved.background;
    Color borderCol = resolved.border;
    const Color textCol = resolved.text;
    Color arrowCol = resolved.arrow;
    const bool active = m_hoveredCollapsed || isFocused() || m_expanded;
    if (active) {
        bgCol = scaledColor(bgCol, 0.80f, 1.06f);
        borderCol = scaledColor(borderCol, 1.14f, 1.18f);
        arrowCol = scaledColor(arrowCol, 1.12f, 1.0f);
    }

    const float ax = getAbsoluteX(ctx);
    const float ay = getAbsoluteY(ctx);
    const float aw = width * scaleX;
    const float ah = height * scaleY;

    bgCol[3] *= alpha;
    drawSolidRect(ax, ay, aw, ah, bgCol);

    borderCol[3] *= alpha;
    constexpr float borderWidth = 1.0f;
    drawSolidRect(ax, ay + ah - borderWidth, aw, borderWidth, borderCol);
    drawSolidRect(ax, ay, aw, borderWidth, borderCol);
    drawSolidRect(ax, ay + borderWidth, borderWidth,
                  ah - borderWidth * 2.0f, borderCol);
    drawSolidRect(ax + aw - borderWidth, ay + borderWidth, borderWidth,
                  ah - borderWidth * 2.0f, borderCol);

    Color wellCol = scaledColor(bgCol, active ? 0.78f : 0.88f, 1.0f);
    wellCol[3] = std::min(1.0f, wellCol[3] * 0.84f);
    drawSolidRect(ax + aw - 32.0f, ay + 1.0f, 31.0f, ah - 2.0f, wellCol);

    arrowCol[3] *= alpha;
    const float arrowCenterX = ax + aw - 16.0f;
    const float arrowCenterY = ay + ah * 0.5f;
    constexpr float rowHeight = 1.2f;
    for (int row = 0; row < 4; ++row) {
        const float rowWidth = m_expanded
            ? 8.0f - static_cast<float>(row) * 2.0f
            : 2.0f + static_cast<float>(row) * 2.0f;
        const float rowY = arrowCenterY - 2.4f + static_cast<float>(row) * rowHeight;
        drawSolidRect(arrowCenterX - rowWidth * 0.5f, rowY,
                      rowWidth, rowHeight, arrowCol);
    }

    // Selected text
    if (ctx.textRenderer) {
        const std::string& text = getSelectedText();
        if (!text.empty()) {
            std::array<float, 4> tc = textCol;
            tc[3] *= alpha;
            float textY = ay + (ah - ctx.textRenderer->measureText(text, 2.0f).height) * 0.5f;
            ctx.textRenderer->draw(ctx, text, ax + 10.0f, textY, 2.0f, tc);
        }
    }
}

void UIDropdown::renderExpanded(const UIRenderContext& ctx) const {
    if (m_options.empty()) {
        return;
    }
    const bool record = ctx.phase == UIRenderPhase::Record;
    if (record && (ctx.commandList == nullptr ||
                   !ctx.panelQuadVertexBuffer.isValid() ||
                   !ctx.panelSolidPipeline.isValid())) {
        return;
    }

    const UIResolvedDropdownStyle resolved = resolveStyle(ctx);
    const Color bgCol = resolved.background;
    const Color borderCol = resolved.border;
    const Color hoverCol = resolved.itemHover;
    const Color selectedCol = resolved.itemSelected;
    const Color separatorCol = resolved.separator;
    const Color accentCol = accentFromSelection(selectedCol);
    const Color textCol = resolved.text;
    const float itemHeight = resolved.itemHeight;
    const bool useGlass = ctx.panelGlassPipeline.isValid() &&
                          ctx.panelGlassBindGroup.isValid() &&
                          ctx.backdropBlurView.isValid() &&
                          ctx.backdropSourceWidth > 0 &&
                          ctx.backdropSourceHeight > 0 &&
                          ctx.backdropBlurWidth > 0 &&
                          ctx.backdropBlurHeight > 0;

    // A restrained per-row wash keeps the popup readable without flattening the glass.
    std::array<float, 4> itemBgCol = bgCol;
    itemBgCol[0] = std::min(1.0f, itemBgCol[0] + 0.018f);
    itemBgCol[1] = std::min(1.0f, itemBgCol[1] + 0.020f);
    itemBgCol[2] = std::min(1.0f, itemBgCol[2] + 0.016f);

    const float ax = getAbsoluteX(ctx);
    const float ay = getAbsoluteY(ctx);
    const float aw = width * scaleX;
    const float ah = height * scaleY;

    const int visibleCount = std::min(static_cast<int>(m_options.size()), m_maxVisibleItems);
    const float panelH = visibleCount * itemHeight;
    float panelY = ay - panelH - 2.0f;

    if (panelY < 0.0f) {
        panelY = ay + ah + 2.0f;
    }

    const float expandAlpha = m_expandTween.isRunning() ? m_expandTween.value() : 1.0f;
    const RhiRect2D scissor = dropdownScissor(ctx);

    if (record && useGlass) {
        const float tintStrength = std::clamp(bgCol[3] * 0.34f, 0.16f, 0.34f);
        const PanelGlassPushConstants pushConstants{
            glm::vec4(static_cast<float>(ctx.screenWidth),
                      static_cast<float>(ctx.screenHeight), ax, panelY),
            glm::vec4(aw, panelH, 0.0f,
                      std::clamp(alpha * expandAlpha * 0.96f, 0.0f, 1.0f)),
            glm::vec4(bgCol[0], bgCol[1], bgCol[2], tintStrength),
            glm::vec4(0.54f, 0.70f, 0.0f, 0.0f)
        };
        ctx.commandList->setGraphicsPipeline(ctx.panelGlassPipeline);
        ctx.commandList->setVertexBuffer(0u, ctx.panelQuadVertexBuffer, 0u);
        ctx.commandList->setBindGroup(0u, ctx.panelGlassBindGroup);
        ctx.commandList->setScissor(scissor);
        ctx.commandList->pushConstants(&pushConstants, sizeof(pushConstants),
                                       rhiFlag(RhiShaderStage::Vertex) |
                                       rhiFlag(RhiShaderStage::Fragment));
        ctx.commandList->draw(6u, 1u, 0u, 0u);
    }

    if (record) {
        ctx.commandList->setGraphicsPipeline(ctx.panelSolidPipeline);
        ctx.commandList->setVertexBuffer(0u, ctx.panelQuadVertexBuffer, 0u);
        ctx.commandList->setScissor(scissor);
    }

    auto drawSolidRect = [&](const float x,
                             const float y,
                             const float rectWidth,
                             const float rectHeight,
                             const Color& rectColor) {
        if (rectWidth <= 0.0f || rectHeight <= 0.0f || rectColor[3] <= 0.0f) {
            return;
        }
        if (!record) {
            return;
        }
        const PanelSolidPushConstants pushConstants{
            glm::vec4(static_cast<float>(ctx.screenWidth),
                      static_cast<float>(ctx.screenHeight), x, y),
            glm::vec4(rectWidth, rectHeight, 0.0f, 0.0f),
            glm::vec4(rectColor[0], rectColor[1], rectColor[2], rectColor[3])
        };
        ctx.commandList->pushConstants(&pushConstants, sizeof(pushConstants),
                                       rhiFlag(RhiShaderStage::Vertex) |
                                       rhiFlag(RhiShaderStage::Fragment));
        ctx.commandList->draw(6u, 1u, 0u, 0u);
    };

    Color panelColor = bgCol;
    panelColor[3] *= alpha * expandAlpha * (useGlass ? 0.42f : 1.0f);
    drawSolidRect(ax, panelY, aw, panelH, panelColor);

    Color panelBorder = borderCol;
    panelBorder[3] *= alpha * expandAlpha;
    constexpr float borderWidth = 1.0f;
    drawSolidRect(ax, panelY + panelH - borderWidth, aw, borderWidth, panelBorder);
    drawSolidRect(ax, panelY, aw, borderWidth, panelBorder);
    drawSolidRect(ax, panelY + borderWidth, borderWidth,
                  panelH - borderWidth * 2.0f, panelBorder);
    drawSolidRect(ax + aw - borderWidth, panelY + borderWidth, borderWidth,
                  panelH - borderWidth * 2.0f, panelBorder);

    // Option items
    const int scrollItems = static_cast<int>(m_scrollOffset / itemHeight);

    for (int i = 0; i < visibleCount && (scrollItems + i) < static_cast<int>(m_options.size()); ++i) {
        const int optIdx = scrollItems + i;
        const float itemY = panelY + panelH - (i + 1) * itemHeight;
        const bool isSelected = (optIdx == m_selectedIndex);
        const bool isHovered = (optIdx == m_hoveredOption);

        // 1. Per-item base background
        Color itemBase = itemBgCol;
        itemBase[3] *= alpha * expandAlpha * (useGlass ? 0.24f : 0.48f);
        drawSolidRect(ax, itemY, aw, itemHeight, itemBase);

        // 2. Selected item background
        if (isSelected) {
            Color selected = selectedCol;
            selected[3] *= alpha * expandAlpha;
            drawSolidRect(ax + 2.0f, itemY + 1.0f,
                          aw - 4.0f, itemHeight - 2.0f, selected);
        }

        // 3. Selected left accent bar
        if (isSelected) {
            Color accent = accentCol;
            accent[3] *= alpha * expandAlpha;
            drawSolidRect(ax + 2.0f, itemY + 3.0f,
                          2.0f, itemHeight - 6.0f, accent);
        }

        // 4. Hover highlight (on top of selected bg)
        if (isHovered) {
            Color hover = m_hoverColorTween.isRunning()
                ? m_hoverColorTween.value() : hoverCol;
            hover[3] *= alpha * expandAlpha * (useGlass ? 0.84f : 1.0f);
            drawSolidRect(ax + 3.0f, itemY + 1.0f,
                          aw - 6.0f, itemHeight - 2.0f, hover);
        }

        // 5. Separator line (not after last visible item)
        if (i < visibleCount - 1 && (scrollItems + i + 1) < static_cast<int>(m_options.size())) {
            Color separator = separatorCol;
            separator[3] *= alpha * expandAlpha * (useGlass ? 0.66f : 1.0f);
            drawSolidRect(ax + 8.0f, itemY, aw - 16.0f, 1.0f, separator);
        }
    }

    // Text pass: render after all geometry to avoid TextRenderer clobbering GL state
    if (ctx.textRenderer) {
        for (int i = 0; i < visibleCount && (scrollItems + i) < static_cast<int>(m_options.size()); ++i) {
            int optIdx = scrollItems + i;
            float itemY = panelY + panelH - (i + 1) * itemHeight;
            bool isSelected = (optIdx == m_selectedIndex);

            std::array<float, 4> tc = textCol;
            tc[3] *= alpha * expandAlpha;
            if (isSelected) {
                tc = {textCol[0], textCol[1], textCol[2], tc[3]};
            }
            float textX = isSelected ? ax + 10.0f : ax + 8.0f;
            float textY = itemY + (itemHeight - ctx.textRenderer->measureText(m_options[optIdx], 2.0f).height) * 0.5f;
            ctx.textRenderer->draw(ctx, m_options[optIdx], textX, textY, 2.0f, tc);
        }
    }
}

int UIDropdown::hitTestOption(float px, float py, const UIRenderContext& ctx) const {
    const float itemHeight = resolveStyle(ctx).itemHeight;
    float ax = getAbsoluteX(ctx);
    float ay = getAbsoluteY(ctx);
    float aw = width * scaleX;
    float ah = height * scaleY;
    float flippedY = static_cast<float>(ctx.screenHeight) - py;

    int visibleCount = std::min(static_cast<int>(m_options.size()), m_maxVisibleItems);
    float panelH = visibleCount * itemHeight;
    float panelY = ay - panelH - 2.0f;
    if (panelY < 0.0f) {
        panelY = ay + ah + 2.0f;
    }

    if (px < ax || px > ax + aw || flippedY < panelY || flippedY > panelY + panelH) {
        return -1;
    }

    int scrollItems = static_cast<int>(m_scrollOffset / itemHeight);
    float relY = flippedY - panelY;
    int itemIdx = static_cast<int>((panelH - relY) / itemHeight) + scrollItems;
    if (itemIdx >= 0 && itemIdx < static_cast<int>(m_options.size())) {
        return itemIdx;
    }
    return -1;
}

bool UIDropdown::hitTestExpandedPanel(float px, float py, const UIRenderContext& ctx) const {
    const float itemHeight = resolveStyle(ctx).itemHeight;
    float ax = getAbsoluteX(ctx);
    float ay = getAbsoluteY(ctx);
    float aw = width * scaleX;
    float ah = height * scaleY;
    float flippedY = static_cast<float>(ctx.screenHeight) - py;

    int visibleCount = std::min(static_cast<int>(m_options.size()), m_maxVisibleItems);
    float panelH = visibleCount * itemHeight;
    float panelY = ay - panelH - 2.0f;
    if (panelY < 0.0f) {
        panelY = ay + ah + 2.0f;
    }

    return px >= ax && px <= ax + aw && flippedY >= panelY && flippedY <= panelY + panelH;
}

UIEventResult UIDropdown::onInput(const UIInputEvent& event, const UIRenderContext& ctx) {
    if (!visible || !interactive) return UIEventResult::Ignored;

    UIEventResult childResult = UIWidget::onInput(event, ctx);
    if (childResult == UIEventResult::Consumed) return UIEventResult::Consumed;

    bool insideCollapsed = hitTest(event.x, event.y, ctx);

    if (m_expanded) {
        switch (event.type) {
            case UIInputEventType::PointerMove: {
                m_hoveredCollapsed = insideCollapsed;
                int newHovered = hitTestOption(event.x, event.y, ctx);
                if (newHovered != m_hoveredOption) {
                    m_prevHoveredOption = m_hoveredOption;
                    m_hoveredOption = newHovered;
                    const Color hoverCol = resolveStyle(ctx).itemHover;
                    if (m_hoveredOption >= 0) {
                        auto fromCol = (m_prevHoveredOption >= 0) ? hoverCol : std::array<float, 4>{hoverCol[0], hoverCol[1], hoverCol[2], 0.0f};
                        m_hoverColorTween.start(fromCol, hoverCol, 0.1f, EasingType::EaseOut);
                    }
                }
                return UIEventResult::Handled;
            }
            case UIInputEventType::PointerDown: {
                if (event.button == UIPointerButton::Primary) {
                    int optIdx = hitTestOption(event.x, event.y, ctx);
                    if (optIdx >= 0) {
                        m_selectedIndex = optIdx;
                        m_expanded = false;
                        m_hoveredCollapsed = false;
                        m_hoveredOption = -1;
                        m_scrollOffset = 0.0f;
                        if (m_onSelectionChanged) m_onSelectionChanged(m_selectedIndex, m_options[m_selectedIndex]);
                        return UIEventResult::Consumed;
                    }
                    if (!insideCollapsed) {
                        m_expanded = false;
                        m_hoveredCollapsed = false;
                        m_hoveredOption = -1;
                        m_scrollOffset = 0.0f;
                        return UIEventResult::Consumed;
                    }
                }
                break;
            }
            case UIInputEventType::Scroll: {
                if (hitTestExpandedPanel(event.x, event.y, ctx) || insideCollapsed) {
                    const float itemHeight = resolveStyle(ctx).itemHeight;
                    int visibleCount = std::min(static_cast<int>(m_options.size()), m_maxVisibleItems);
                    float maxScroll = std::max(0.0f, static_cast<float>(m_options.size() - visibleCount) * itemHeight);
                    m_scrollOffset = std::clamp(m_scrollOffset - static_cast<float>(event.scrollY) * itemHeight,
                                                0.0f, maxScroll);
                    return UIEventResult::Handled;
                }
                break;
            }
            case UIInputEventType::Command: {
                if (event.command == UICommand::Cancel) {
                    m_expanded = false;
                    m_hoveredCollapsed = false;
                    m_hoveredOption = -1;
                    m_scrollOffset = 0.0f;
                    return UIEventResult::Consumed;
                }
                if (event.command == UICommand::NavigateUp) {
                    m_hoveredOption = std::max(0, (m_hoveredOption < 0 ? m_selectedIndex : m_hoveredOption) - 1);
                    return UIEventResult::Consumed;
                }
                if (event.command == UICommand::NavigateDown) {
                    int maxIdx = static_cast<int>(m_options.size()) - 1;
                    m_hoveredOption = std::min(maxIdx, (m_hoveredOption < 0 ? m_selectedIndex : m_hoveredOption) + 1);
                    return UIEventResult::Consumed;
                }
                if (event.command == UICommand::Activate && m_hoveredOption >= 0) {
                    m_selectedIndex = m_hoveredOption;
                    m_expanded = false;
                    m_hoveredCollapsed = false;
                    m_hoveredOption = -1;
                    m_scrollOffset = 0.0f;
                    if (m_onSelectionChanged) m_onSelectionChanged(m_selectedIndex, m_options[m_selectedIndex]);
                    return UIEventResult::Consumed;
                }
                break;
            }
            default:
                break;
        }
        return UIEventResult::Handled;
    }

    // Collapsed state
    switch (event.type) {
        case UIInputEventType::PointerMove: {
            m_hoveredCollapsed = insideCollapsed;
            return insideCollapsed ? UIEventResult::Handled : UIEventResult::Ignored;
        }
        case UIInputEventType::PointerDown: {
            if (event.button == UIPointerButton::Primary && insideCollapsed) {
                m_expanded = true;
                m_hoveredCollapsed = true;
                m_hoveredOption = m_selectedIndex;
                return UIEventResult::Consumed;
            }
            break;
        }
        case UIInputEventType::Command: {
            if (isFocused() && event.command == UICommand::Activate) {
                m_expanded = true;
                m_hoveredOption = m_selectedIndex;
                return UIEventResult::Consumed;
            }
            break;
        }
        default:
            break;
    }

    return UIEventResult::Ignored;
}

UIDropdownStyle UIDropdown::resolveBaseStyle(const UIRenderContext& ctx) const {
    if (m_hasLocalStyle) {
        return m_localStyle;
    }

    UIDropdownStyle style = UIStyleResolver::dropdownStyleFromTheme(ctx.theme);
    style.itemHeight = m_itemHeight;
    if (!ctx.theme) {
        style.background = m_bgColor;
        style.border = m_borderColor;
        style.text = {1.0f, 1.0f, 1.0f, 1.0f};
        style.arrow = m_arrowColor;
        style.itemHover = m_itemHoverColor;
        style.itemSelected = {0.15f, 0.45f, 0.55f, 0.35f};
        style.separator = {0.35f, 0.35f, 0.35f, 0.4f};
        style.accent = {0.2f, 0.8f, 1.0f, 1.0f};
    }
    return style;
}

UIResolvedDropdownStyle UIDropdown::resolveStyle(const UIRenderContext& ctx) const {
    return UIStyleResolver::resolveDropdown(resolveBaseStyle(ctx));
}
