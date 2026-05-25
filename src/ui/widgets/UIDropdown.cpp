#include "UIDropdown.h"

#include <algorithm>

#include <glad/glad.h>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include "../../renderer/Shader.h"
#include "../../resource/ResourceMgr.h"
#include "../font/TextRenderer.h"
#include "../core/UIRenderUtils.h"

UIDropdown::UIDropdown() {
    interactive = true;
    focusable = true;
    width = 200.0f;
    height = 32.0f;
}

UIDropdown::~UIDropdown() { shutdown(); }

void UIDropdown::init(ResourceMgr& resourceMgr) {
    m_shader = resourceMgr.getShader("ui_color");
    initMesh();
    m_expandTween.setImmediate(0.0f);
    m_hoverColorTween.setImmediate({0.30f, 0.30f, 0.30f, 1.0f});
}

void UIDropdown::shutdown() {
    cleanupMesh();
    m_shader = nullptr;
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

void UIDropdown::updateAnimations(float dt) {
    m_expandTween.tick(dt);
    m_hoverColorTween.tick(dt);
    UIWidget::updateAnimations(dt);
}

void UIDropdown::initMesh() {
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    // Collapsed: bg (6) + border (24) + arrow (6) = 36
    // Expanded: panel bg (6) + panel border (24) + per-item: base(8*6) + selected(8*6) + bar(8*6) + sep(8*6) + hover(8*6)
    // ~320 verts * 2 floats, pre-allocate generously
    glBufferData(GL_ARRAY_BUFFER, 320 * 2 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void UIDropdown::cleanupMesh() {
    if (m_vao != 0) {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    if (m_vbo != 0) {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
}

void UIDropdown::renderSelf(const UIRenderContext& ctx) const {
    if (!m_shader || m_vao == 0) return;

    const UIRenderUtils::GLStateGuard glState;
    m_shader->use();
    m_shader->setVec2("uScreenSize", glm::vec2(static_cast<float>(ctx.screenWidth),
                                                static_cast<float>(ctx.screenHeight)));

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

    renderCollapsed(ctx);

    if (m_expanded || m_expandTween.isRunning()) {
        renderExpanded(ctx);
    }

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void UIDropdown::renderCollapsed(const UIRenderContext& ctx) const {
    const UITheme* theme = ctx.theme;
    const auto& bgCol = theme ? theme->dropdownBackground : m_bgColor;
    const auto& borderCol = theme ? theme->dropdownBorder : m_borderColor;
    const auto& textCol = theme ? theme->textPrimary : std::array<float, 4>{1, 1, 1, 1};
    const auto& arrowCol = theme ? theme->dropdownArrow : m_arrowColor;

    float ax = getAbsoluteX(ctx);
    float ay = getAbsoluteY(ctx);
    float aw = width * scaleX;
    float ah = height * scaleY;

    // Background
    {
        std::array<float, 4> c = bgCol;
        c[3] *= alpha;
        m_shader->setVec4("uColor", glm::vec4(c[0], c[1], c[2], c[3]));
        float verts[] = {
            ax, ay,  ax + aw, ay,  ax + aw, ay + ah,
            ax, ay,  ax + aw, ay + ah,  ax, ay + ah,
        };
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    // Border (24 verts starting at vertex 6)
    {
        std::array<float, 4> c = borderCol;
        c[3] *= alpha;
        m_shader->setVec4("uColor", glm::vec4(c[0], c[1], c[2], c[3]));
        float bw = 1.0f;
        float verts[48];
        verts[0]  = ax;         verts[1]  = ay+ah-bw;  verts[2]  = ax+aw;      verts[3]  = ay+ah-bw;
        verts[4]  = ax+aw;      verts[5]  = ay+ah;     verts[6]  = ax;         verts[7]  = ay+ah-bw;
        verts[8]  = ax+aw;      verts[9]  = ay+ah;     verts[10] = ax;         verts[11] = ay+ah;
        verts[12] = ax;         verts[13] = ay;        verts[14] = ax+aw;      verts[15] = ay;
        verts[16] = ax+aw;      verts[17] = ay+bw;     verts[18] = ax;         verts[19] = ay;
        verts[20] = ax+aw;      verts[21] = ay+bw;     verts[22] = ax;         verts[23] = ay+bw;
        verts[24] = ax;         verts[25] = ay;        verts[26] = ax+bw;      verts[27] = ay;
        verts[28] = ax+bw;      verts[29] = ay+ah;     verts[30] = ax;         verts[31] = ay;
        verts[32] = ax+bw;      verts[33] = ay+ah;     verts[34] = ax;         verts[35] = ay+ah;
        verts[36] = ax+aw-bw;   verts[37] = ay;        verts[38] = ax+aw;      verts[39] = ay;
        verts[40] = ax+aw;      verts[41] = ay+ah;     verts[42] = ax+aw-bw;   verts[43] = ay;
        verts[44] = ax+aw;      verts[45] = ay+ah;     verts[46] = ax+aw-bw;   verts[47] = ay+ah;
        glBufferSubData(GL_ARRAY_BUFFER, 6 * 2 * sizeof(float), sizeof(verts), verts);
        glDrawArrays(GL_TRIANGLES, 6, 24);
    }

    // Arrow indicator (downward triangle at right side)
    {
        std::array<float, 4> c = arrowCol;
        c[3] *= alpha;
        m_shader->setVec4("uColor", glm::vec4(c[0], c[1], c[2], c[3]));
        float arrowSize = 8.0f;
        float acx = ax + aw - 16.0f;
        float acy = ay + ah * 0.5f;
        float verts[] = {
            acx - arrowSize * 0.5f, acy + arrowSize * 0.3f,
            acx + arrowSize * 0.5f, acy + arrowSize * 0.3f,
            acx, acy - arrowSize * 0.3f,
        };
        glBufferSubData(GL_ARRAY_BUFFER, 30 * 2 * sizeof(float), sizeof(verts), verts);
        glDrawArrays(GL_TRIANGLES, 30, 3);
    }

    // Selected text
    if (ctx.textRenderer) {
        const std::string& text = getSelectedText();
        if (!text.empty()) {
            std::array<float, 4> tc = textCol;
            tc[3] *= alpha;
            float textY = ay + (ah - ctx.textRenderer->measureText(text, 2.0f).height) * 0.5f;
            ctx.textRenderer->render(text, ax + 8.0f, textY, 2.0f, tc,
                                     static_cast<float>(ctx.screenWidth),
                                     static_cast<float>(ctx.screenHeight));
        }
    }
}

void UIDropdown::renderExpanded(const UIRenderContext& ctx) const {
    if (m_options.empty()) return;

    // Re-bind our shader/VAO — renderCollapsed's TextRenderer call clobbers them
    m_shader->use();
    m_shader->setVec2("uScreenSize", glm::vec2(static_cast<float>(ctx.screenWidth),
                                                static_cast<float>(ctx.screenHeight)));
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

    const UITheme* theme = ctx.theme;
    const auto& bgCol = theme ? theme->dropdownBackground : m_bgColor;
    const auto& borderCol = theme ? theme->dropdownBorder : m_borderColor;
    const auto& hoverCol = theme ? theme->dropdownItemHover : m_itemHoverColor;
    const auto& selectedCol = theme ? theme->dropdownItemSelected : std::array<float, 4>{0.15f, 0.45f, 0.55f, 0.35f};
    const auto& separatorCol = theme ? theme->dropdownSeparator : std::array<float, 4>{0.35f, 0.35f, 0.35f, 0.4f};
    const auto& accentCol = theme ? theme->accentPrimary : std::array<float, 4>{0.2f, 0.8f, 1.0f, 1.0f};
    const auto& textCol = theme ? theme->textPrimary : std::array<float, 4>{1, 1, 1, 1};

    // Item base background: slightly brighter than panel bg
    std::array<float, 4> itemBgCol = bgCol;
    itemBgCol[0] = std::min(1.0f, itemBgCol[0] + 0.04f);
    itemBgCol[1] = std::min(1.0f, itemBgCol[1] + 0.04f);
    itemBgCol[2] = std::min(1.0f, itemBgCol[2] + 0.04f);

    float ax = getAbsoluteX(ctx);
    float ay = getAbsoluteY(ctx);
    float aw = width * scaleX;
    float ah = height * scaleY;

    int visibleCount = std::min(static_cast<int>(m_options.size()), m_maxVisibleItems);
    float panelH = visibleCount * m_itemHeight;
    float panelY = ay - panelH - 2.0f;

    if (panelY < 0.0f) {
        panelY = ay + ah + 2.0f;
    }

    float expandAlpha = m_expandTween.isRunning() ? m_expandTween.value() : 1.0f;

    // Panel background
    {
        std::array<float, 4> c = bgCol;
        c[3] *= alpha * expandAlpha;
        m_shader->setVec4("uColor", glm::vec4(c[0], c[1], c[2], c[3]));
        float verts[] = {
            ax, panelY,  ax + aw, panelY,  ax + aw, panelY + panelH,
            ax, panelY,  ax + aw, panelY + panelH,  ax, panelY + panelH,
        };
        glBufferSubData(GL_ARRAY_BUFFER, 33 * 2 * sizeof(float), sizeof(verts), verts);
        glDrawArrays(GL_TRIANGLES, 33, 6);
    }

    // Panel border
    {
        std::array<float, 4> c = borderCol;
        c[3] *= alpha * expandAlpha;
        m_shader->setVec4("uColor", glm::vec4(c[0], c[1], c[2], c[3]));
        float bw = 1.0f;
        float verts[48];
        verts[0]  = ax;         verts[1]  = panelY+panelH-bw; verts[2]  = ax+aw;      verts[3]  = panelY+panelH-bw;
        verts[4]  = ax+aw;      verts[5]  = panelY+panelH;    verts[6]  = ax;         verts[7]  = panelY+panelH-bw;
        verts[8]  = ax+aw;      verts[9]  = panelY+panelH;    verts[10] = ax;         verts[11] = panelY+panelH;
        verts[12] = ax;         verts[13] = panelY;           verts[14] = ax+aw;      verts[15] = panelY;
        verts[16] = ax+aw;      verts[17] = panelY+bw;        verts[18] = ax;         verts[19] = panelY;
        verts[20] = ax+aw;      verts[21] = panelY+bw;        verts[22] = ax;         verts[23] = panelY+bw;
        verts[24] = ax;         verts[25] = panelY;           verts[26] = ax+bw;      verts[27] = panelY;
        verts[28] = ax+bw;      verts[29] = panelY+panelH;    verts[30] = ax;         verts[31] = panelY;
        verts[32] = ax+bw;      verts[33] = panelY+panelH;    verts[34] = ax;         verts[35] = panelY+panelH;
        verts[36] = ax+aw-bw;   verts[37] = panelY;           verts[38] = ax+aw;      verts[39] = panelY;
        verts[40] = ax+aw;      verts[41] = panelY+panelH;    verts[42] = ax+aw-bw;   verts[43] = panelY;
        verts[44] = ax+aw;      verts[45] = panelY+panelH;    verts[46] = ax+aw-bw;   verts[47] = panelY+panelH;
        glBufferSubData(GL_ARRAY_BUFFER, 39 * 2 * sizeof(float), sizeof(verts), verts);
        glDrawArrays(GL_TRIANGLES, 39, 24);
    }

    // Option items
    int scrollItems = static_cast<int>(m_scrollOffset / m_itemHeight);
    constexpr int kItemBaseOffset = 63;   // per-item base bg
    constexpr int kItemSelectedOffset = 111; // per-item selected bg
    constexpr int kItemBarOffset = 159;   // per-item selected left bar
    constexpr int kItemSepOffset = 207;   // per-item separator
    constexpr int kItemHoverOffset = 255; // per-item hover highlight

    for (int i = 0; i < visibleCount && (scrollItems + i) < static_cast<int>(m_options.size()); ++i) {
        int optIdx = scrollItems + i;
        float itemY = panelY + panelH - (i + 1) * m_itemHeight;
        bool isSelected = (optIdx == m_selectedIndex);
        bool isHovered = (optIdx == m_hoveredOption);

        // 1. Per-item base background
        {
            std::array<float, 4> c = itemBgCol;
            c[3] *= alpha * expandAlpha;
            m_shader->setVec4("uColor", glm::vec4(c[0], c[1], c[2], c[3]));
            float verts[] = {
                ax, itemY,  ax + aw, itemY,  ax + aw, itemY + m_itemHeight,
                ax, itemY,  ax + aw, itemY + m_itemHeight,  ax, itemY + m_itemHeight,
            };
            glBufferSubData(GL_ARRAY_BUFFER, (kItemBaseOffset + i * 6) * 2 * sizeof(float), sizeof(verts), verts);
            glDrawArrays(GL_TRIANGLES, kItemBaseOffset + i * 6, 6);
        }

        // 2. Selected item background
        if (isSelected) {
            std::array<float, 4> c = selectedCol;
            c[3] *= alpha * expandAlpha;
            m_shader->setVec4("uColor", glm::vec4(c[0], c[1], c[2], c[3]));
            float verts[] = {
                ax, itemY,  ax + aw, itemY,  ax + aw, itemY + m_itemHeight,
                ax, itemY,  ax + aw, itemY + m_itemHeight,  ax, itemY + m_itemHeight,
            };
            glBufferSubData(GL_ARRAY_BUFFER, (kItemSelectedOffset + i * 6) * 2 * sizeof(float), sizeof(verts), verts);
            glDrawArrays(GL_TRIANGLES, kItemSelectedOffset + i * 6, 6);
        }

        // 3. Selected left accent bar
        if (isSelected) {
            std::array<float, 4> c = accentCol;
            c[3] *= alpha * expandAlpha;
            m_shader->setVec4("uColor", glm::vec4(c[0], c[1], c[2], c[3]));
            float barW = 2.0f;
            float verts[] = {
                ax, itemY,  ax + barW, itemY,  ax + barW, itemY + m_itemHeight,
                ax, itemY,  ax + barW, itemY + m_itemHeight,  ax, itemY + m_itemHeight,
            };
            glBufferSubData(GL_ARRAY_BUFFER, (kItemBarOffset + i * 6) * 2 * sizeof(float), sizeof(verts), verts);
            glDrawArrays(GL_TRIANGLES, kItemBarOffset + i * 6, 6);
        }

        // 4. Hover highlight (on top of selected bg)
        if (isHovered) {
            std::array<float, 4> c = m_hoverColorTween.isRunning() ? m_hoverColorTween.value() : hoverCol;
            c[3] *= alpha * expandAlpha;
            m_shader->setVec4("uColor", glm::vec4(c[0], c[1], c[2], c[3]));
            float verts[] = {
                ax, itemY,  ax + aw, itemY,  ax + aw, itemY + m_itemHeight,
                ax, itemY,  ax + aw, itemY + m_itemHeight,  ax, itemY + m_itemHeight,
            };
            glBufferSubData(GL_ARRAY_BUFFER, (kItemHoverOffset + i * 6) * 2 * sizeof(float), sizeof(verts), verts);
            glDrawArrays(GL_TRIANGLES, kItemHoverOffset + i * 6, 6);
        }

        // 5. Separator line (not after last visible item)
        if (i < visibleCount - 1 && (scrollItems + i + 1) < static_cast<int>(m_options.size())) {
            std::array<float, 4> c = separatorCol;
            c[3] *= alpha * expandAlpha;
            m_shader->setVec4("uColor", glm::vec4(c[0], c[1], c[2], c[3]));
            float verts[] = {
                ax + 4.0f, itemY,  ax + aw - 4.0f, itemY,  ax + aw - 4.0f, itemY + 1.0f,
                ax + 4.0f, itemY,  ax + aw - 4.0f, itemY + 1.0f,  ax + 4.0f, itemY + 1.0f,
            };
            glBufferSubData(GL_ARRAY_BUFFER, (kItemSepOffset + i * 6) * 2 * sizeof(float), sizeof(verts), verts);
            glDrawArrays(GL_TRIANGLES, kItemSepOffset + i * 6, 6);
        }
    }

    // Text pass: render after all geometry to avoid TextRenderer clobbering GL state
    if (ctx.textRenderer) {
        for (int i = 0; i < visibleCount && (scrollItems + i) < static_cast<int>(m_options.size()); ++i) {
            int optIdx = scrollItems + i;
            float itemY = panelY + panelH - (i + 1) * m_itemHeight;
            bool isSelected = (optIdx == m_selectedIndex);

            std::array<float, 4> tc = textCol;
            tc[3] *= alpha * expandAlpha;
            if (isSelected) {
                tc = {accentCol[0], accentCol[1], accentCol[2], tc[3]};
            }
            float textX = isSelected ? ax + 10.0f : ax + 8.0f;
            float textY = itemY + (m_itemHeight - ctx.textRenderer->measureText(m_options[optIdx], 2.0f).height) * 0.5f;
            ctx.textRenderer->render(m_options[optIdx], textX, textY, 2.0f, tc,
                                     static_cast<float>(ctx.screenWidth),
                                     static_cast<float>(ctx.screenHeight));
        }
    }
}

int UIDropdown::hitTestOption(float px, float py, const UIRenderContext& ctx) const {
    float ax = getAbsoluteX(ctx);
    float ay = getAbsoluteY(ctx);
    float aw = width * scaleX;
    float ah = height * scaleY;
    float flippedY = static_cast<float>(ctx.screenHeight) - py;

    int visibleCount = std::min(static_cast<int>(m_options.size()), m_maxVisibleItems);
    float panelH = visibleCount * m_itemHeight;
    float panelY = ay - panelH - 2.0f;
    if (panelY < 0.0f) {
        panelY = ay + ah + 2.0f;
    }

    if (px < ax || px > ax + aw || flippedY < panelY || flippedY > panelY + panelH) {
        return -1;
    }

    int scrollItems = static_cast<int>(m_scrollOffset / m_itemHeight);
    float relY = flippedY - panelY;
    int itemIdx = static_cast<int>((panelH - relY) / m_itemHeight) + scrollItems;
    if (itemIdx >= 0 && itemIdx < static_cast<int>(m_options.size())) {
        return itemIdx;
    }
    return -1;
}

bool UIDropdown::hitTestExpandedPanel(float px, float py, const UIRenderContext& ctx) const {
    float ax = getAbsoluteX(ctx);
    float ay = getAbsoluteY(ctx);
    float aw = width * scaleX;
    float ah = height * scaleY;
    float flippedY = static_cast<float>(ctx.screenHeight) - py;

    int visibleCount = std::min(static_cast<int>(m_options.size()), m_maxVisibleItems);
    float panelH = visibleCount * m_itemHeight;
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
                int newHovered = hitTestOption(event.x, event.y, ctx);
                if (newHovered != m_hoveredOption) {
                    m_prevHoveredOption = m_hoveredOption;
                    m_hoveredOption = newHovered;
                    const auto& hoverCol = ctx.theme ? ctx.theme->dropdownItemHover : m_itemHoverColor;
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
                        m_hoveredOption = -1;
                        m_scrollOffset = 0.0f;
                        if (m_onSelectionChanged) m_onSelectionChanged(m_selectedIndex, m_options[m_selectedIndex]);
                        return UIEventResult::Consumed;
                    }
                    if (!insideCollapsed) {
                        m_expanded = false;
                        m_hoveredOption = -1;
                        m_scrollOffset = 0.0f;
                        return UIEventResult::Consumed;
                    }
                }
                break;
            }
            case UIInputEventType::Scroll: {
                if (hitTestExpandedPanel(event.x, event.y, ctx) || insideCollapsed) {
                    int visibleCount = std::min(static_cast<int>(m_options.size()), m_maxVisibleItems);
                    float maxScroll = std::max(0.0f, static_cast<float>(m_options.size() - visibleCount) * m_itemHeight);
                    m_scrollOffset = std::clamp(m_scrollOffset - static_cast<float>(event.scrollY) * m_itemHeight,
                                                0.0f, maxScroll);
                    return UIEventResult::Handled;
                }
                break;
            }
            case UIInputEventType::Command: {
                if (event.command == UICommand::Cancel) {
                    m_expanded = false;
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
            return insideCollapsed ? UIEventResult::Handled : UIEventResult::Ignored;
        }
        case UIInputEventType::PointerDown: {
            if (event.button == UIPointerButton::Primary && insideCollapsed) {
                m_expanded = true;
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
