#include "UIContextMenu.h"

#include <algorithm>
#include <cmath>

#include "../font/TextRenderer.h"
#include "../../renderer/rhi/RhiCommandList.h"

#include <glm/vec4.hpp>

namespace {

struct ContextMenuSolidPushConstants {
    glm::vec4 screenRect;
    glm::vec4 rectRadius;
    glm::vec4 color;
};

struct ContextMenuGlassPushConstants {
    glm::vec4 screenRect;
    glm::vec4 extentOpacity;
    glm::vec4 tint;
    glm::vec4 appearance;
};

static_assert(sizeof(ContextMenuSolidPushConstants) == 48u);
static_assert(sizeof(ContextMenuGlassPushConstants) == 64u);

[[nodiscard]] RhiRect2D contextMenuScissor(const UIRenderContext& context) {
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

UIContextMenu::UIContextMenu() {
    interactive = true;
    focusable = false;
    // The context menu positions itself absolutely; these are overridden on show().
    width = 180.0f;
    height = 100.0f;
    anchor = Anchor::BottomLeft;
}

UIContextMenu::~UIContextMenu() {
    shutdown();
}

void UIContextMenu::init(GameResources& resources, RhiDevice& rhiDevice) {
    m_showTween.start(0.0f, 0.0f, 0.15f, EasingType::EaseOut);

    UIWidget::init(resources, rhiDevice);
}

void UIContextMenu::shutdown() {
    m_items.clear();
    UIWidget::shutdown();
}

int UIContextMenu::addItem(const std::string& text, std::function<void()> onClick) {
    MenuItem item;
    item.type = ItemType::Entry;
    item.text = text;
    item.onClick = std::move(onClick);
    const int index = static_cast<int>(m_items.size());
    m_items.push_back(std::move(item));
    return index;
}

void UIContextMenu::addSeparator() {
    MenuItem item;
    item.type = ItemType::Separator;
    m_items.push_back(std::move(item));
}

void UIContextMenu::show(float menuX, float menuY) {
    m_menuVisible = true;
    m_menuX = menuX;
    m_menuY = menuY;
    m_hoveredItem = -1;
    m_scrollOffset = 0.0f;
    m_showTween.start(0.0f, 1.0f, 0.15f, EasingType::EaseOut);

    const UIResolvedContextMenuStyle resolved =
        UIStyleResolver::resolveContextMenu(m_hasLocalStyle ? m_localStyle : UIContextMenuStyle{});
    width = resolved.width;
    height = menuHeight(resolved);
}

void UIContextMenu::hide() {
    m_menuVisible = false;
    m_showTween.start(m_showTween.value(), 0.0f, 0.1f, EasingType::EaseIn);
}

void UIContextMenu::setStyle(const UIContextMenuStyle& style) {
    m_localStyle = style;
    m_hasLocalStyle = true;
}

void UIContextMenu::clearLocalStyle() {
    m_hasLocalStyle = false;
}

void UIContextMenu::updateAnimations(float dt) {
    m_showTween.tick(dt);
    UIWidget::updateAnimations(dt);
}

int UIContextMenu::hitTestItem(float px, float py, const UIRenderContext& ctx) const {
    const UIResolvedContextMenuStyle resolved = resolveStyle(ctx);
    const float flippedY = static_cast<float>(ctx.screenHeight) - py;
    const float menuW = resolved.width;
    const float menuH = menuHeight(resolved);

    // Menu position: m_menuX is the left edge, m_menuY is the top edge (in screen coords, Y-down).
    // Convert to widget coords: the menu's bottom-left in widget space.
    const float menuLeft = m_menuX;
    const float menuTop_widget = static_cast<float>(ctx.screenHeight) - m_menuY; // Top in widget coords.
    const float menuBottom_widget = menuTop_widget - menuH;

    if (px < menuLeft || px >= menuLeft + menuW)
        return -1;
    if (flippedY < menuBottom_widget || flippedY >= menuTop_widget)
        return -1;

    float yOff = resolved.padding;
    for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
        const float itemH = (m_items[i].type == ItemType::Separator) ? resolved.separatorHeight : resolved.itemHeight;
        // Check from top down.
        const float itemTop = menuTop_widget - yOff;
        const float itemBottom = itemTop - itemH;
        if (flippedY >= itemBottom && flippedY < itemTop) {
            return (m_items[i].type == ItemType::Entry) ? i : -1;
        }
        yOff += itemH;
    }
    return -1;
}

void UIContextMenu::renderSelf(const UIRenderContext& ctx) const {
    // Context menu rendering is handled in render() override.
    (void)ctx;
}

void UIContextMenu::render(const UIRenderContext& ctx) const {
    if (!visible || !m_menuVisible)
        return;
    if (m_showTween.value() < 0.01f)
        return;
    const bool record = ctx.phase == UIRenderPhase::Record;
    if (record &&
        (ctx.commandList == nullptr || !ctx.panelQuadVertexBuffer.isValid() || !ctx.panelSolidPipeline.isValid())) {
        return;
    }

    const UIResolvedContextMenuStyle resolved = resolveStyle(ctx);
    const Color bgCol = resolved.background;
    const Color brdCol = resolved.border;
    const Color hovCol = resolved.itemHover;
    const Color sepCol = resolved.separator;
    const Color txtCol = resolved.text;
    const float menuW = resolved.width;
    const float menuH = menuHeight(resolved);
    const float brdW = resolved.borderWidth;

    const float menuAlpha = m_showTween.value();
    const float menuLeft = m_menuX;
    const float menuTop_screen = m_menuY;
    const float menuTop_widget = static_cast<float>(ctx.screenHeight) - menuTop_screen;
    const float menuBottom_widget = menuTop_widget - menuH;
    const bool useGlass = ctx.panelGlassPipeline.isValid() && ctx.panelGlassBindGroup.isValid() &&
                          ctx.backdropBlurView.isValid() && ctx.backdropSourceWidth > 0 &&
                          ctx.backdropSourceHeight > 0 && ctx.backdropBlurWidth > 0 && ctx.backdropBlurHeight > 0;

    if (record) {
        RhiCommandList& commandList = *ctx.commandList;
        const RhiRect2D scissor = contextMenuScissor(ctx);

        if (useGlass) {
            const float tintStrength = std::clamp(bgCol[3] * 0.34f, 0.16f, 0.34f);
            const ContextMenuGlassPushConstants pushConstants{
                glm::vec4(static_cast<float>(ctx.screenWidth), static_cast<float>(ctx.screenHeight), menuLeft,
                          menuBottom_widget),
                glm::vec4(menuW, menuH, 0.0f, std::clamp(menuAlpha * 0.96f, 0.0f, 1.0f)),
                glm::vec4(bgCol[0], bgCol[1], bgCol[2], tintStrength), glm::vec4(0.54f, 0.70f, 0.0f, 0.0f)};

            commandList.setGraphicsPipeline(ctx.panelGlassPipeline);
            commandList.setVertexBuffer(0u, ctx.panelQuadVertexBuffer, 0u);
            commandList.setBindGroup(0u, ctx.panelGlassBindGroup);
            commandList.setScissor(scissor);
            commandList.pushConstants(&pushConstants, sizeof(pushConstants),
                                      rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment));
            commandList.draw(6u, 1u, 0u, 0u);
        }

        commandList.setGraphicsPipeline(ctx.panelSolidPipeline);
        commandList.setVertexBuffer(0u, ctx.panelQuadVertexBuffer, 0u);
        commandList.setScissor(scissor);

        auto drawSolidRect = [&](const float rectX, const float rectY, const float rectWidth, const float rectHeight,
                                 const Color& rectColor) {
            if (rectWidth <= 0.0f || rectHeight <= 0.0f || rectColor[3] <= 0.0f) {
                return;
            }
            const ContextMenuSolidPushConstants pushConstants{
                glm::vec4(static_cast<float>(ctx.screenWidth), static_cast<float>(ctx.screenHeight), rectX, rectY),
                glm::vec4(rectWidth, rectHeight, 0.0f, 0.0f),
                glm::vec4(rectColor[0], rectColor[1], rectColor[2], rectColor[3])};
            commandList.pushConstants(&pushConstants, sizeof(pushConstants),
                                      rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment));
            commandList.draw(6u, 1u, 0u, 0u);
        };

        // Background.
        const float bgAlphaScale = useGlass ? 0.42f : 1.0f;
        Color background = bgCol;
        background[3] *= menuAlpha * bgAlphaScale;
        drawSolidRect(menuLeft, menuBottom_widget, menuW, menuH, background);

        // Border.
        Color border = brdCol;
        border[3] *= menuAlpha;
        drawSolidRect(menuLeft, menuTop_widget - brdW, menuW, brdW, border);
        drawSolidRect(menuLeft, menuBottom_widget, menuW, brdW, border);
        drawSolidRect(menuLeft, menuBottom_widget, brdW, menuH, border);
        drawSolidRect(menuLeft + menuW - brdW, menuBottom_widget, brdW, menuH, border);

        // Item highlights and separators.
        float yOff = resolved.padding;
        for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
            const float itemH =
                (m_items[i].type == ItemType::Separator) ? resolved.separatorHeight : resolved.itemHeight;
            const float itemTop = menuTop_widget - yOff;
            const float itemBottom = itemTop - itemH;
            if (m_items[i].type == ItemType::Entry && i == m_hoveredItem) {
                const float hoverAlphaScale = useGlass ? 0.84f : 1.0f;
                Color hover = hovCol;
                hover[3] *= menuAlpha * hoverAlphaScale;
                drawSolidRect(menuLeft + 3.0f, itemBottom + 1.0f, menuW - 6.0f, itemH - 2.0f, hover);
            } else if (m_items[i].type == ItemType::Separator) {
                const float separatorAlphaScale = useGlass ? 0.66f : 1.0f;
                Color separator = sepCol;
                separator[3] *= menuAlpha * separatorAlphaScale;
                const float separatorY = itemTop - itemH * 0.5f;
                drawSolidRect(menuLeft + 8.0f, separatorY - 0.5f, menuW - 16.0f, 1.0f, separator);
            }
            yOff += itemH;
        }
    }

    // Render text.
    if (ctx.textRenderer) {
        const float textScale = 1.0f;
        float yOff = resolved.padding;
        for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
            const float itemH =
                (m_items[i].type == ItemType::Separator) ? resolved.separatorHeight : resolved.itemHeight;
            if (m_items[i].type == ItemType::Entry) {
                const float textY = menuTop_widget - yOff - itemH * 0.5f -
                                    ctx.textRenderer->measureText(m_items[i].text, textScale).height * 0.5f;
                ctx.textRenderer->draw(ctx, m_items[i].text, menuLeft + 12.0f, textY, textScale,
                                       {txtCol[0], txtCol[1], txtCol[2], txtCol[3] * menuAlpha});
            }
            yOff += itemH;
        }
    }
}

UIEventResult UIContextMenu::onInput(const UIInputEvent& event, const UIRenderContext& ctx) {
    if (!visible || !m_menuVisible)
        return UIEventResult::Ignored;

    switch (event.type) {
    case UIInputEventType::PointerMove: {
        m_hoveredItem = hitTestItem(event.x, event.y, ctx);
        return (m_hoveredItem >= 0) ? UIEventResult::Handled : UIEventResult::Ignored;
    }

    case UIInputEventType::PointerDown: {
        if (event.button == UIPointerButton::Primary) {
            const int idx = hitTestItem(event.x, event.y, ctx);
            if (idx >= 0) {
                // Execute the item's callback.
                if (m_items[idx].onClick)
                    m_items[idx].onClick();
                hide();
                return UIEventResult::Consumed;
            }
            // Click outside the menu closes it.
            hide();
            return UIEventResult::Consumed;
        }
        break;
    }

    case UIInputEventType::Command:
        if (event.command == UICommand::Cancel) {
            hide();
            return UIEventResult::Consumed;
        }
        break;

    default: break;
    }

    return UIEventResult::Ignored;
}

UIContextMenuStyle UIContextMenu::resolveBaseStyle(const UIRenderContext& ctx) const {
    if (m_hasLocalStyle) {
        return m_localStyle;
    }
    return UIStyleResolver::contextMenuStyleFromTheme(ctx.theme);
}

UIResolvedContextMenuStyle UIContextMenu::resolveStyle(const UIRenderContext& ctx) const {
    return UIStyleResolver::resolveContextMenu(resolveBaseStyle(ctx));
}

float UIContextMenu::menuHeight(const UIResolvedContextMenuStyle& style) const {
    float totalH = style.padding * 2.0f;
    for (const auto& item : m_items) {
        totalH += (item.type == ItemType::Separator) ? style.separatorHeight : style.itemHeight;
    }
    return totalH;
}
