#include "UICheckbox.h"

#include <algorithm>

#include <glad/glad.h>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include "../renderer/Shader.h"
#include "../resource/ResourceMgr.h"
#include "UIRenderUtils.h"

UICheckbox::UICheckbox() {
    interactive = true;
    focusable = true;
    width = 200.0f;
    height = 24.0f;
}

UICheckbox::~UICheckbox() { shutdown(); }

void UICheckbox::init(ResourceMgr& resourceMgr) {
    m_shader = resourceMgr.getShader("ui_color");
    initMesh();
    m_label.init(resourceMgr);
    m_label.anchor = Anchor::BottomLeft;
    m_checkScaleTween.setImmediate(m_checked ? 1.0f : 0.0f);
}

void UICheckbox::shutdown() {
    m_label.shutdown();
    cleanupMesh();
    m_shader = nullptr;
}

void UICheckbox::setChecked(bool checked) {
    m_checked = checked;
}

bool UICheckbox::isChecked() const {
    return m_checked;
}

void UICheckbox::setLabel(const std::string& text) {
    m_label.setText(text);
}

void UICheckbox::setOnChanged(std::function<void(bool)> callback) {
    m_onChanged = std::move(callback);
}

void UICheckbox::updateAnimations(float dt) {
    m_checkScaleTween.tick(dt);
    UIWidget::updateAnimations(dt);
}

void UICheckbox::initMesh() {
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    // Box (6) + border (24) + check mark (6) = 36 verts * 2 floats
    glBufferData(GL_ARRAY_BUFFER, 36 * 2 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void UICheckbox::cleanupMesh() {
    if (m_vao != 0) {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    if (m_vbo != 0) {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
}

void UICheckbox::renderSelf(const UIRenderContext& ctx) const {
    if (!m_shader || m_vao == 0) return;

    const UITheme* theme = ctx.theme;
    float boxSize = theme ? theme->checkboxSize : 20.0f;

    const auto& boxCol = (m_hovered && theme) ? theme->checkboxBoxHover
                         : (m_hovered ? m_boxHoverColor : (theme ? theme->checkboxBox : m_boxColor));
    const auto& borderCol = theme ? theme->checkboxBoxBorder : m_boxBorderColor;
    const auto& checkCol = theme ? theme->checkboxCheck : m_checkColor;
    const auto& textCol = theme ? theme->textPrimary : m_label.getTextColor();

    float ax = getAbsoluteX(ctx);
    float ay = getAbsoluteY(ctx);
    float ah = height * scaleY;
    float cy = ay + ah * 0.5f;

    float bx0 = ax;
    float by0 = cy - boxSize * 0.5f;
    float bx1 = ax + boxSize;
    float by1 = cy + boxSize * 0.5f;
    float bw = 1.0f;

    const UIRenderUtils::GLStateGuard glState;
    m_shader->use();
    m_shader->setVec2("uScreenSize", glm::vec2(static_cast<float>(ctx.screenWidth),
                                                static_cast<float>(ctx.screenHeight)));

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

    // Box background
    {
        std::array<float, 4> c = boxCol;
        c[3] *= alpha;
        m_shader->setVec4("uColor", glm::vec4(c[0], c[1], c[2], c[3]));
        float verts[] = {
            bx0, by0,  bx1, by0,  bx1, by1,
            bx0, by0,  bx1, by1,  bx0, by1,
        };
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    // Box border (4 quads = 24 verts, starting at vertex 6)
    {
        std::array<float, 4> c = borderCol;
        c[3] *= alpha;
        m_shader->setVec4("uColor", glm::vec4(c[0], c[1], c[2], c[3]));
        float verts[48];
        // Top
        verts[0]  = bx0;       verts[1]  = by1 - bw;  verts[2]  = bx1;       verts[3]  = by1 - bw;
        verts[4]  = bx1;       verts[5]  = by1;        verts[6]  = bx0;       verts[7]  = by1 - bw;
        verts[8]  = bx1;       verts[9]  = by1;        verts[10] = bx0;       verts[11] = by1;
        // Bottom
        verts[12] = bx0;       verts[13] = by0;        verts[14] = bx1;       verts[15] = by0;
        verts[16] = bx1;       verts[17] = by0 + bw;   verts[18] = bx0;       verts[19] = by0;
        verts[20] = bx1;       verts[21] = by0 + bw;   verts[22] = bx0;       verts[23] = by0 + bw;
        // Left
        verts[24] = bx0;       verts[25] = by0;        verts[26] = bx0 + bw;  verts[27] = by0;
        verts[28] = bx0 + bw;  verts[29] = by1;        verts[30] = bx0;       verts[31] = by0;
        verts[32] = bx0 + bw;  verts[33] = by1;        verts[34] = bx0;       verts[35] = by1;
        // Right
        verts[36] = bx1 - bw;  verts[37] = by0;        verts[38] = bx1;       verts[39] = by0;
        verts[40] = bx1;       verts[41] = by1;        verts[42] = bx1 - bw;  verts[43] = by0;
        verts[44] = bx1;       verts[45] = by1;        verts[46] = bx1 - bw;  verts[47] = by1;
        glBufferSubData(GL_ARRAY_BUFFER, 6 * 2 * sizeof(float), sizeof(verts), verts);
        glDrawArrays(GL_TRIANGLES, 6, 24);
    }

    // Check mark (inner filled square, scaled)
    if (m_checked) {
        float scale = m_checkScaleTween.value();
        if (scale > 0.01f) {
            float padding = boxSize * 0.2f;
            float innerSize = (boxSize - padding * 2.0f) * scale;
            float icx = (bx0 + bx1) * 0.5f;
            float icy = (by0 + by1) * 0.5f;
            float ix0 = icx - innerSize * 0.5f;
            float iy0 = icy - innerSize * 0.5f;
            float ix1 = icx + innerSize * 0.5f;
            float iy1 = icy + innerSize * 0.5f;

            std::array<float, 4> c = checkCol;
            c[3] *= alpha;
            m_shader->setVec4("uColor", glm::vec4(c[0], c[1], c[2], c[3]));
            float verts[] = {
                ix0, iy0,  ix1, iy0,  ix1, iy1,
                ix0, iy0,  ix1, iy1,  ix0, iy1,
            };
            glBufferSubData(GL_ARRAY_BUFFER, 30 * 2 * sizeof(float), sizeof(verts), verts);
            glDrawArrays(GL_TRIANGLES, 30, 6);
        }
    }

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // Render label text to the right of the box
    float labelX = bx1 + 8.0f;
    float tw = ctx.textRenderer ? m_label.measureTextWidth(*ctx.textRenderer) : 0.0f;
    float th = ctx.textRenderer ? m_label.measureTextHeight(*ctx.textRenderer) : 0.0f;
    const_cast<UIText&>(m_label).anchorOffsetX = labelX;
    const_cast<UIText&>(m_label).anchorOffsetY = cy - th * 0.5f;
    const_cast<UIText&>(m_label).setTextColor(textCol);
    const_cast<UIText&>(m_label).alpha = alpha;
    m_label.render(ctx);
}

UIEventResult UICheckbox::onInput(const UIInputEvent& event, const UIRenderContext& ctx) {
    if (!visible || !interactive) return UIEventResult::Ignored;

    UIEventResult childResult = UIWidget::onInput(event, ctx);
    if (childResult == UIEventResult::Consumed) return UIEventResult::Consumed;

    bool inside = hitTest(event.x, event.y, ctx);

    switch (event.type) {
        case UIInputEventType::PointerMove: {
            if (inside != m_hovered) {
                m_hovered = inside;
            }
            return inside ? UIEventResult::Handled : UIEventResult::Ignored;
        }
        case UIInputEventType::PointerDown: {
            if (event.button == UIPointerButton::Primary && inside) {
                return UIEventResult::Handled;
            }
            break;
        }
        case UIInputEventType::PointerUp: {
            if (event.button == UIPointerButton::Primary && inside) {
                m_checked = !m_checked;
                m_checkScaleTween.start(m_checked ? 0.0f : 1.0f,
                                        m_checked ? 1.0f : 0.0f,
                                        0.15f, EasingType::EaseOut);
                if (m_onChanged) m_onChanged(m_checked);
                return UIEventResult::Consumed;
            }
            break;
        }
        case UIInputEventType::Command: {
            if (isFocused() && event.command == UICommand::Activate) {
                m_checked = !m_checked;
                m_checkScaleTween.start(m_checked ? 0.0f : 1.0f,
                                        m_checked ? 1.0f : 0.0f,
                                        0.15f, EasingType::EaseOut);
                if (m_onChanged) m_onChanged(m_checked);
                return UIEventResult::Consumed;
            }
            break;
        }
        default:
            break;
    }

    return UIEventResult::Ignored;
}
