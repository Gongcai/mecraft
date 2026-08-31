#include "UITabControl.h"

#include <algorithm>
#include <cmath>
#include <glm/vec4.hpp>

#include "../font/TextRenderer.h"
#include "../../resource/GameResources.h"
#include "../../renderer/rhi/RhiCommandList.h"

namespace {

struct TabSolidPushConstants {
    glm::vec4 screenRect;
    glm::vec4 rectRadius;
    glm::vec4 color;
};

struct TabGlassPushConstants {
    glm::vec4 screenRect;
    glm::vec4 extentOpacity;
    glm::vec4 tint;
    glm::vec4 appearance;
};

static_assert(sizeof(TabSolidPushConstants) == 48u);
static_assert(sizeof(TabGlassPushConstants) == 64u);

[[nodiscard]] RhiRect2D tabScissor(const UIRenderContext& context) {
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

// Simple transparent panel used as a content container for each tab.
class TabContentPanel : public UIWidget {
protected:
    void renderSelf(const UIRenderContext& ctx) const override {
        (void)ctx;
        // Transparent panel — renders children only.
    }
};

UITabControl::UITabControl() {
    interactive = true;
    focusable = false;
    width = 400.0f;
    height = 300.0f;
}

UITabControl::~UITabControl() {
    shutdown();
}

void UITabControl::init(GameResources& resources, RhiDevice& rhiDevice) {
    UIWidget::init(resources, rhiDevice);
}

void UITabControl::shutdown() {
    UIWidget::shutdown();
}

int UITabControl::addTab(const std::string& title) {
    auto panel = std::make_unique<TabContentPanel>();
    panel->visible = static_cast<int>(m_tabs.size()) == m_activeIndex;

    Tab tab;
    tab.title = title;
    tab.contentPanel = panel.get();
    const int index = static_cast<int>(m_tabs.size());

    addChild(std::move(panel));
    m_tabs.push_back(std::move(tab));
    return index;
}

UIWidget* UITabControl::getContentPanel(int index) {
    if (index < 0 || index >= static_cast<int>(m_tabs.size()))
        return nullptr;
    return m_tabs[index].contentPanel;
}

void UITabControl::setActiveTab(int index) {
    if (index < 0 || index >= static_cast<int>(m_tabs.size()))
        return;
    if (m_activeIndex == index)
        return;
    m_activeIndex = index;
    for (int i = 0; i < static_cast<int>(m_tabs.size()); ++i) {
        if (m_tabs[i].contentPanel) {
            m_tabs[i].contentPanel->visible = (i == m_activeIndex);
        }
    }
    if (onTabChanged)
        onTabChanged(m_activeIndex);
}

float UITabControl::getHeaderHeight(const UIRenderContext& ctx) const {
    return resolveStyle(ctx, UIStyleState_Normal).headerHeight;
}

void UITabControl::setHeaderColor(const Color& styleColor) {
    m_headerColor = styleColor;
    m_hasLocalColors = true;
    m_hasLocalStyle = false;
}

void UITabControl::setHeaderActiveColor(const Color& styleColor) {
    m_headerActiveColor = styleColor;
    m_hasLocalColors = true;
    m_hasLocalStyle = false;
}

void UITabControl::setHeaderHoverColor(const Color& styleColor) {
    m_headerHoverColor = styleColor;
    m_hasLocalColors = true;
    m_hasLocalStyle = false;
}

void UITabControl::setIndicatorColor(const Color& styleColor) {
    m_indicatorColor = styleColor;
    m_hasLocalColors = true;
    m_hasLocalStyle = false;
}

void UITabControl::setContentColor(const Color& styleColor) {
    m_contentColor = styleColor;
    m_hasLocalColors = true;
    m_hasLocalStyle = false;
}

void UITabControl::setStyle(const UITabControlStyle& style) {
    m_localStyle = style;
    m_hasLocalStyle = true;
    m_hasLocalColors = false;
}

void UITabControl::clearLocalStyle() {
    m_hasLocalStyle = false;
    m_hasLocalColors = false;
}

int UITabControl::hitTestHeader(float px, float py, const UIRenderContext& ctx) const {
    if (m_tabs.empty())
        return -1;
    const float flippedY = static_cast<float>(ctx.screenHeight) - py;
    const float ax = getAbsoluteX(ctx);
    const float ay = getAbsoluteY(ctx);
    const float aw = width * scaleX;
    const float headerH = resolveStyle(ctx, UIStyleState_Normal).headerHeight;

    // Header area is at the top of the widget.
    if (flippedY < ay + (height * scaleY - headerH) || flippedY >= ay + height * scaleY)
        return -1;
    if (px < ax || px >= ax + aw)
        return -1;

    const float tabW = aw / static_cast<float>(m_tabs.size());
    const float localX = px - ax;
    const int idx = static_cast<int>(localX / tabW);
    if (idx >= 0 && idx < static_cast<int>(m_tabs.size()))
        return idx;
    return -1;
}

void UITabControl::renderSelf(const UIRenderContext& ctx) const {
    // Tab headers are rendered in renderSelf.
    // Content is rendered in render() override.
    if (m_tabs.empty()) {
        return;
    }
    const bool record = ctx.phase == UIRenderPhase::Record;
    if (record &&
        (ctx.commandList == nullptr || !ctx.panelQuadVertexBuffer.isValid() || !ctx.panelSolidPipeline.isValid())) {
        return;
    }

    const UITabControlStyle baseStyle = resolveBaseStyle(ctx);
    const UIResolvedTabControlStyle baseResolved =
        UIStyleResolver::resolveTabControl(baseStyle, interactive ? UIStyleState_Normal : UIStyleState_Disabled);
    const Color indCol = baseResolved.indicator;
    const Color contentCol = baseResolved.content;
    const Color txtCol = baseResolved.text;
    const float headerH = baseResolved.headerHeight;
    const float indicatorH = baseResolved.indicatorHeight;

    const float ax = getAbsoluteX(ctx);
    const float ay = getAbsoluteY(ctx);
    const float aw = width * scaleX;
    const float ah = height * scaleY;
    const float tabW = aw / static_cast<float>(m_tabs.size());

    // Header Y is at the top of the widget (higher Y in OpenGL coords).
    const float headerBottomY = ay + ah - headerH;

    const float indX0 = ax + static_cast<float>(m_activeIndex) * tabW;
    const float indX1 = indX0 + tabW;
    const bool useGlass = ctx.panelGlassPipeline.isValid() && ctx.panelGlassBindGroup.isValid() &&
                          ctx.backdropBlurView.isValid() && ctx.backdropSourceWidth > 0 &&
                          ctx.backdropSourceHeight > 0 && ctx.backdropBlurWidth > 0 && ctx.backdropBlurHeight > 0;

    if (record) {
        RhiCommandList& commandList = *ctx.commandList;
        const RhiRect2D scissor = tabScissor(ctx);

        if (useGlass) {
            const float tintStrength = std::clamp(contentCol[3] * 0.40f, 0.18f, 0.40f);
            const TabGlassPushConstants pushConstants{
                glm::vec4(static_cast<float>(ctx.screenWidth), static_cast<float>(ctx.screenHeight), ax, ay),
                glm::vec4(aw, headerBottomY - ay, 0.0f, std::clamp(alpha * 0.94f, 0.0f, 1.0f)),
                glm::vec4(contentCol[0], contentCol[1], contentCol[2], tintStrength),
                glm::vec4(0.58f, 0.74f, 0.0f, 0.0f)};
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

        auto drawSolidRect = [&](const float x, const float y, const float rectWidth, const float rectHeight,
                                 const Color& color) {
            if (rectWidth <= 0.0f || rectHeight <= 0.0f || color[3] <= 0.0f) {
                return;
            }
            const TabSolidPushConstants pushConstants{
                glm::vec4(static_cast<float>(ctx.screenWidth), static_cast<float>(ctx.screenHeight), x, y),
                glm::vec4(rectWidth, rectHeight, 0.0f, 0.0f), glm::vec4(color[0], color[1], color[2], color[3])};
            commandList.pushConstants(&pushConstants, sizeof(pushConstants),
                                      rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment));
            commandList.draw(6u, 1u, 0u, 0u);
        };

        // Draw content background, then headers and the active indicator.
        Color contentDraw = contentCol;
        contentDraw[3] *= alpha * (useGlass ? 0.34f : 1.0f);
        drawSolidRect(ax, ay, aw, headerBottomY - ay, contentDraw);

        for (int i = 0; i < static_cast<int>(m_tabs.size()); ++i) {
            int tabState =
                interactive ? static_cast<int>(UIStyleState_Normal) : static_cast<int>(UIStyleState_Disabled);
            if (i == m_activeIndex) {
                tabState |= static_cast<int>(UIStyleState_Selected);
            } else if (i == m_hoveredTab) {
                tabState |= static_cast<int>(UIStyleState_Hovered);
            }
            Color col = UIStyleResolver::resolveTabControl(baseStyle, tabState).header;
            col[3] *= alpha;
            const float x0 = ax + static_cast<float>(i) * tabW;
            drawSolidRect(x0, headerBottomY, tabW, headerH, col);
        }

        // Draw indicator.
        Color indicatorDraw = indCol;
        indicatorDraw[3] *= alpha;
        drawSolidRect(indX0, headerBottomY - indicatorH, indX1 - indX0, indicatorH, indicatorDraw);
    }

    // Render header text.
    if (ctx.textRenderer) {
        const float textScale = 1.45f;
        for (int i = 0; i < static_cast<int>(m_tabs.size()); ++i) {
            const auto m = ctx.textRenderer->measureText(m_tabs[i].title, textScale);
            const float x0 = ax + static_cast<float>(i) * tabW;
            const float textX = x0 + (tabW - m.width) * 0.5f;
            const float textY = headerBottomY + (headerH - m.height) * 0.5f;
            ctx.textRenderer->draw(ctx, m_tabs[i].title, textX, textY, textScale,
                                   {txtCol[0], txtCol[1], txtCol[2], txtCol[3] * alpha});
        }
    }
}

void UITabControl::render(const UIRenderContext& ctx) const {
    if (!visible)
        return;

    // Render the tab headers (renderSelf).
    renderSelf(ctx);

    // Render the active tab's content panel below the header.
    if (m_activeIndex >= 0 && m_activeIndex < static_cast<int>(m_tabs.size())) {
        auto& panel = m_tabs[m_activeIndex].contentPanel;
        if (panel) {
            const float headerH = resolveStyle(ctx, UIStyleState_Normal).headerHeight;
            // headerH is in scaled pixels; convert to local widget space.
            const float headerHLocal = headerH / (scaleY > 0.0f ? scaleY : 1.0f);
            panel->anchor = Anchor::BottomLeft;
            panel->x = 0;
            panel->y = 0;
            panel->width = width;
            panel->height = height - headerHLocal;
            panel->render(ctx);
        }
    }

    // Content panels are owned as children for layout/focus purposes, but the
    // active panel is rendered manually above so inactive tabs stay hidden.
}

UIEventResult UITabControl::onInput(const UIInputEvent& event, const UIRenderContext& ctx) {
    if (!visible || !interactive)
        return UIEventResult::Ignored;

    UIEventResult aggregate = UIEventResult::Ignored;

    // Forward to active tab content first.
    if (m_activeIndex >= 0 && m_activeIndex < static_cast<int>(m_tabs.size())) {
        auto& panel = m_tabs[m_activeIndex].contentPanel;
        if (panel) {
            const UIEventResult contentResult = panel->onInput(event, ctx);
            if (contentResult == UIEventResult::Consumed)
                return UIEventResult::Consumed;
            if (contentResult == UIEventResult::Handled)
                aggregate = UIEventResult::Handled;
        }
    }

    switch (event.type) {
    case UIInputEventType::PointerMove: {
        const int idx = hitTestHeader(event.x, event.y, ctx);
        m_hoveredTab = idx;
        return (idx >= 0) ? UIEventResult::Handled : UIEventResult::Ignored;
    }

    case UIInputEventType::PointerDown:
        if (event.button == UIPointerButton::Primary) {
            const int idx = hitTestHeader(event.x, event.y, ctx);
            if (idx >= 0) {
                setActiveTab(idx);
                return UIEventResult::Consumed;
            }
        }
        break;

    case UIInputEventType::Command:
        if (event.command == UICommand::TabLeft) {
            const int newIndex =
                (m_activeIndex - 1 + static_cast<int>(m_tabs.size())) % static_cast<int>(m_tabs.size());
            setActiveTab(newIndex);
            return UIEventResult::Consumed;
        }
        if (event.command == UICommand::TabRight) {
            const int newIndex = (m_activeIndex + 1) % static_cast<int>(m_tabs.size());
            setActiveTab(newIndex);
            return UIEventResult::Consumed;
        }
        break;

    default: break;
    }

    return aggregate;
}

UITabControlStyle UITabControl::resolveBaseStyle(const UIRenderContext& ctx) const {
    if (m_hasLocalStyle) {
        return m_localStyle;
    }

    if (m_hasLocalColors) {
        UITabControlStyle style = UIStyleResolver::tabControlStyleFromTheme(ctx.theme);
        style.headerNormal = m_headerColor;
        style.headerActive = m_headerActiveColor;
        style.headerHover = m_headerHoverColor;
        style.indicator = m_indicatorColor;
        style.content = m_contentColor;
        return style;
    }

    return UIStyleResolver::tabControlStyleFromTheme(ctx.theme);
}

UIResolvedTabControlStyle UITabControl::resolveStyle(const UIRenderContext& ctx, int state) const {
    return UIStyleResolver::resolveTabControl(resolveBaseStyle(ctx), state);
}
