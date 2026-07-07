#include "UIContextMenu.h"

#include <glad/glad.h>
#include <algorithm>

#include "../core/UIRenderUtils.h"
#include "../font/TextRenderer.h"
#include "../../resource/ResourceMgr.h"
#include "../../renderer/core/Shader.h"
#include "../../renderer/rhi/gl/GlRhiTextureRegistry.h"

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

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

void UIContextMenu::init(ResourceMgr& resourceMgr) {
    m_shader = resourceMgr.getShader("ui_color");
    m_glassShader = resourceMgr.getShader("ui_glass");

    // Buffer: background (6) + border (24) + items (up to 32 * 6) + separator lines (up to 16 * 6)
    // Generous allocation.
    constexpr int totalFloats = (6 + 24 + 32 * 6 + 16 * 6) * 2;
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(totalFloats * sizeof(float)),
                 nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);

    m_showTween.start(0.0f, 0.0f, 0.15f, EasingType::EaseOut);

    UIWidget::init(resourceMgr);
}

void UIContextMenu::shutdown() {
    cleanupMesh();
    m_shader = nullptr;
    m_glassShader = nullptr;
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

void UIContextMenu::cleanupMesh() {
    if (m_vao) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    if (m_vbo) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
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

    if (px < menuLeft || px >= menuLeft + menuW) return -1;
    if (flippedY < menuBottom_widget || flippedY >= menuTop_widget) return -1;

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
    if (!visible || !m_menuVisible) return;
    if (m_showTween.value() < 0.01f) return;

    const UIRenderUtils::GLStateGuard guard;

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
    const uint32_t backdropBlurTextureId = renderer::rhi::gl::textureId(ctx.backdropBlur);
    const bool useGlass = m_glassShader &&
                          backdropBlurTextureId != 0 &&
                          ctx.backdropSourceWidth > 0 &&
                          ctx.backdropSourceHeight > 0 &&
                          ctx.backdropBlurWidth > 0 &&
                          ctx.backdropBlurHeight > 0;

    // Build vertices.
    std::vector<float> verts;
    // Background (offset 0).
    UIRenderUtils::pushColorQuad(verts, menuLeft, menuBottom_widget,
                                 menuLeft + menuW, menuTop_widget);
    // Border (offset 6, 24 verts).
    UIRenderUtils::pushColorQuad(verts, menuLeft, menuTop_widget - brdW,
                                 menuLeft + menuW, menuTop_widget);          // top
    UIRenderUtils::pushColorQuad(verts, menuLeft, menuBottom_widget,
                                 menuLeft + menuW, menuBottom_widget + brdW); // bottom
    UIRenderUtils::pushColorQuad(verts, menuLeft, menuBottom_widget,
                                 menuLeft + brdW, menuTop_widget);            // left
    UIRenderUtils::pushColorQuad(verts, menuLeft + menuW - brdW, menuBottom_widget,
                                 menuLeft + menuW, menuTop_widget);           // right

    // Item hover highlights and separator lines.
    float yOff = resolved.padding;
    for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
        const float itemH = (m_items[i].type == ItemType::Separator) ? resolved.separatorHeight : resolved.itemHeight;
        const float itemTop = menuTop_widget - yOff;
        const float itemBottom = itemTop - itemH;

        if (m_items[i].type == ItemType::Entry && i == m_hoveredItem) {
            UIRenderUtils::pushColorQuad(verts, menuLeft + 3.0f, itemBottom + 1.0f,
                                         menuLeft + menuW - 3.0f, itemTop - 1.0f);
        } else if (m_items[i].type == ItemType::Separator) {
            const float sepY = itemTop - itemH * 0.5f;
            UIRenderUtils::pushColorQuad(verts, menuLeft + 8.0f, sepY - 0.5f,
                                         menuLeft + menuW - 8.0f, sepY + 0.5f);
        }
        yOff += itemH;
    }

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    static_cast<GLsizeiptr>(verts.size() * sizeof(float)), verts.data());

    if (useGlass) {
        const float tintStrength = std::clamp(bgCol[3] * 0.34f, 0.16f, 0.34f);

        m_glassShader->use();
        m_glassShader->setVec2("uScreenSize",
                               glm::vec2(static_cast<float>(ctx.screenWidth),
                                         static_cast<float>(ctx.screenHeight)));
        m_glassShader->setVec2("uBackdropSize",
                               glm::vec2(static_cast<float>(ctx.backdropSourceWidth),
                                         static_cast<float>(ctx.backdropSourceHeight)));
        m_glassShader->setVec4("uTint", glm::vec4(bgCol[0], bgCol[1], bgCol[2], tintStrength));
        m_glassShader->setFloat("uOpacity", std::clamp(menuAlpha * 0.96f, 0.0f, 1.0f));
        m_glassShader->setFloat("uSaturation", 0.54f);
        m_glassShader->setFloat("uDarken", 0.70f);
        m_glassShader->setInt("uBackdrop", 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, backdropBlurTextureId);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    m_shader->use();
    m_shader->setVec2("uScreenSize",
                      glm::vec2(static_cast<float>(ctx.screenWidth),
                                static_cast<float>(ctx.screenHeight)));

    // Background.
    const float bgAlphaScale = useGlass ? 0.42f : 1.0f;
    m_shader->setVec4("uColor", glm::vec4(bgCol[0], bgCol[1], bgCol[2], bgCol[3] * menuAlpha * bgAlphaScale));
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // Border.
    m_shader->setVec4("uColor", glm::vec4(brdCol[0], brdCol[1], brdCol[2], brdCol[3] * menuAlpha));
    glDrawArrays(GL_TRIANGLES, 6, 24);

    // Item highlights and separators.
    int vertIdx = 30;
    yOff = resolved.padding;
    for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
        const float itemH = (m_items[i].type == ItemType::Separator) ? resolved.separatorHeight : resolved.itemHeight;
        if (m_items[i].type == ItemType::Entry && i == m_hoveredItem) {
            const float hoverAlphaScale = useGlass ? 0.84f : 1.0f;
            m_shader->setVec4("uColor", glm::vec4(hovCol[0], hovCol[1], hovCol[2],
                                                  hovCol[3] * menuAlpha * hoverAlphaScale));
            glDrawArrays(GL_TRIANGLES, vertIdx, 6);
            vertIdx += 6;
        } else if (m_items[i].type == ItemType::Separator) {
            const float separatorAlphaScale = useGlass ? 0.66f : 1.0f;
            m_shader->setVec4("uColor", glm::vec4(sepCol[0], sepCol[1], sepCol[2],
                                                  sepCol[3] * menuAlpha * separatorAlphaScale));
            glDrawArrays(GL_TRIANGLES, vertIdx, 6);
            vertIdx += 6;
        }
        yOff += itemH;
    }

    glBindVertexArray(0);

    // Render text.
    if (ctx.textRenderer) {
        const float textScale = 1.0f;
        yOff = resolved.padding;
        for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
            const float itemH = (m_items[i].type == ItemType::Separator) ? resolved.separatorHeight : resolved.itemHeight;
            if (m_items[i].type == ItemType::Entry) {
                const float textY = menuTop_widget - yOff - itemH * 0.5f -
                                    ctx.textRenderer->measureText(m_items[i].text, textScale).height * 0.5f;
                ctx.textRenderer->render(m_items[i].text,
                                         menuLeft + 12.0f, textY, textScale,
                                         {txtCol[0], txtCol[1], txtCol[2], txtCol[3] * menuAlpha},
                                         static_cast<float>(ctx.screenWidth),
                                         static_cast<float>(ctx.screenHeight));
            }
            yOff += itemH;
        }
    }
}

UIEventResult UIContextMenu::onInput(const UIInputEvent& event, const UIRenderContext& ctx) {
    if (!visible || !m_menuVisible) return UIEventResult::Ignored;

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
                if (m_items[idx].onClick) m_items[idx].onClick();
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

    default:
        break;
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
