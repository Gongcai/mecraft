#include "UITabControl.h"

#include <glad/glad.h>
#include <algorithm>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include "../core/UIRenderUtils.h"
#include "../font/TextRenderer.h"
#include "../../resource/ResourceMgr.h"
#include "../../renderer/core/Shader.h"
#include "../../renderer/rhi/gl/GlRhiTextureRegistry.h"

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

void UITabControl::init(ResourceMgr& resourceMgr) {
    m_shader = resourceMgr.getShader("ui_color");
    m_glassShader = resourceMgr.getShader("ui_glass");
    // Mesh for: header bg rects (6 verts per tab) + indicator rect (6 verts) + content bg (6 verts).
    // We'll buffer-sub-data dynamically, so allocate generously.
    constexpr int maxTabs = 32;
    constexpr int totalFloats = (maxTabs * 6 + 6 + 6) * 2;
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(totalFloats * sizeof(float)),
                 nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);

    UIWidget::init(resourceMgr);
}

void UITabControl::shutdown() {
    if (m_vao) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    if (m_vbo) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
    m_shader = nullptr;
    m_glassShader = nullptr;
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
    if (index < 0 || index >= static_cast<int>(m_tabs.size())) return nullptr;
    return m_tabs[index].contentPanel;
}

void UITabControl::setActiveTab(int index) {
    if (index < 0 || index >= static_cast<int>(m_tabs.size())) return;
    if (m_activeIndex == index) return;
    m_activeIndex = index;
    for (int i = 0; i < static_cast<int>(m_tabs.size()); ++i) {
        if (m_tabs[i].contentPanel) {
            m_tabs[i].contentPanel->visible = (i == m_activeIndex);
        }
    }
    if (onTabChanged) onTabChanged(m_activeIndex);
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
    if (m_tabs.empty()) return -1;
    const float flippedY = static_cast<float>(ctx.screenHeight) - py;
    const float ax = getAbsoluteX(ctx);
    const float ay = getAbsoluteY(ctx);
    const float aw = width * scaleX;
    const float headerH = resolveStyle(ctx, UIStyleState_Normal).headerHeight;

    // Header area is at the top of the widget.
    if (flippedY < ay + (height * scaleY - headerH) || flippedY >= ay + height * scaleY) return -1;
    if (px < ax || px >= ax + aw) return -1;

    const float tabW = aw / static_cast<float>(m_tabs.size());
    const float localX = px - ax;
    const int idx = static_cast<int>(localX / tabW);
    if (idx >= 0 && idx < static_cast<int>(m_tabs.size())) return idx;
    return -1;
}

void UITabControl::renderSelf(const UIRenderContext& ctx) const {
    // Tab headers are rendered in renderSelf.
    // Content is rendered in render() override.
    if (!m_shader || m_tabs.empty()) return;

    const UIRenderUtils::GLStateGuard guard;
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
    const float headerTopY = ay + ah;
    const float headerBottomY = ay + ah - headerH;

    // Build header vertices.
    std::vector<float> verts;
    verts.reserve((m_tabs.size() * 6 + 12) * 2);
    UIRenderUtils::pushColorQuad(verts, ax, ay, ax + aw, headerBottomY);
    for (int i = 0; i < static_cast<int>(m_tabs.size()); ++i) {
        const float x0 = ax + static_cast<float>(i) * tabW;
        const float x1 = x0 + tabW;
        UIRenderUtils::pushColorQuad(verts, x0, headerBottomY, x1, headerTopY);
    }
    // Indicator rect (below active header).
    const float indX0 = ax + static_cast<float>(m_activeIndex) * tabW;
    const float indX1 = indX0 + tabW;
    UIRenderUtils::pushColorQuad(verts, indX0, headerBottomY - indicatorH,
                                 indX1, headerBottomY);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    static_cast<GLsizeiptr>(verts.size() * sizeof(float)), verts.data());

    const uint32_t backdropBlurTextureId = renderer::rhi::gl::textureId(ctx.backdropBlur);
    const bool useGlass = m_glassShader &&
                          backdropBlurTextureId != 0 &&
                          ctx.backdropSourceWidth > 0 &&
                          ctx.backdropSourceHeight > 0;

    if (useGlass) {
        const float tintStrength = std::clamp(contentCol[3] * 0.40f, 0.18f, 0.40f);
        m_glassShader->use();
        m_glassShader->setVec2("uScreenSize",
                               glm::vec2(static_cast<float>(ctx.screenWidth),
                                         static_cast<float>(ctx.screenHeight)));
        m_glassShader->setVec2("uBackdropSize",
                               glm::vec2(static_cast<float>(ctx.backdropSourceWidth),
                                         static_cast<float>(ctx.backdropSourceHeight)));
        m_glassShader->setVec4("uTint", glm::vec4(contentCol[0], contentCol[1], contentCol[2], tintStrength));
        m_glassShader->setFloat("uOpacity", std::clamp(alpha * 0.94f, 0.0f, 1.0f));
        m_glassShader->setFloat("uSaturation", 0.58f);
        m_glassShader->setFloat("uDarken", 0.74f);
        m_glassShader->setInt("uBackdrop", 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, backdropBlurTextureId);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    m_shader->use();
    m_shader->setVec2("uScreenSize",
                      glm::vec2(static_cast<float>(ctx.screenWidth),
                                static_cast<float>(ctx.screenHeight)));

    // Draw content background, then headers and the active indicator.
    Color contentDraw = contentCol;
    contentDraw[3] *= useGlass ? 0.34f : 1.0f;
    m_shader->setVec4("uColor",
                      glm::vec4(contentDraw[0], contentDraw[1], contentDraw[2], contentDraw[3] * alpha));
    glDrawArrays(GL_TRIANGLES, 0, 6);

    const GLint headerVertexOffset = 6;
    for (int i = 0; i < static_cast<int>(m_tabs.size()); ++i) {
        int tabState = interactive ? static_cast<int>(UIStyleState_Normal) : static_cast<int>(UIStyleState_Disabled);
        if (i == m_activeIndex) {
            tabState |= static_cast<int>(UIStyleState_Selected);
        } else if (i == m_hoveredTab) {
            tabState |= static_cast<int>(UIStyleState_Hovered);
        }
        const Color col = UIStyleResolver::resolveTabControl(baseStyle, tabState).header;
        m_shader->setVec4("uColor", glm::vec4(col[0], col[1], col[2], col[3] * alpha));
        glDrawArrays(GL_TRIANGLES, headerVertexOffset + i * 6, 6);
    }

    // Draw indicator.
    m_shader->setVec4("uColor", glm::vec4(indCol[0], indCol[1], indCol[2], indCol[3] * alpha));
    glDrawArrays(GL_TRIANGLES, headerVertexOffset + static_cast<GLint>(m_tabs.size() * 6), 6);

    glBindVertexArray(0);

    // Render header text.
    if (ctx.textRenderer) {
        const float textScale = 1.45f;
        for (int i = 0; i < static_cast<int>(m_tabs.size()); ++i) {
            const auto m = ctx.textRenderer->measureText(m_tabs[i].title, textScale);
            const float x0 = ax + static_cast<float>(i) * tabW;
            const float textX = x0 + (tabW - m.width) * 0.5f;
            const float textY = headerBottomY + (headerH - m.height) * 0.5f;
            ctx.textRenderer->render(m_tabs[i].title, textX, textY, textScale,
                                     {txtCol[0], txtCol[1], txtCol[2], txtCol[3] * alpha},
                                     static_cast<float>(ctx.screenWidth),
                                     static_cast<float>(ctx.screenHeight));
        }
    }
}

void UITabControl::render(const UIRenderContext& ctx) const {
    if (!visible) return;

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
    if (!visible || !interactive) return UIEventResult::Ignored;

    UIEventResult aggregate = UIEventResult::Ignored;

    // Forward to active tab content first.
    if (m_activeIndex >= 0 && m_activeIndex < static_cast<int>(m_tabs.size())) {
        auto& panel = m_tabs[m_activeIndex].contentPanel;
        if (panel) {
            const UIEventResult contentResult = panel->onInput(event, ctx);
            if (contentResult == UIEventResult::Consumed) return UIEventResult::Consumed;
            if (contentResult == UIEventResult::Handled) aggregate = UIEventResult::Handled;
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
            const int newIndex = (m_activeIndex - 1 + static_cast<int>(m_tabs.size())) % static_cast<int>(m_tabs.size());
            setActiveTab(newIndex);
            return UIEventResult::Consumed;
        }
        if (event.command == UICommand::TabRight) {
            const int newIndex = (m_activeIndex + 1) % static_cast<int>(m_tabs.size());
            setActiveTab(newIndex);
            return UIEventResult::Consumed;
        }
        break;

    default:
        break;
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
