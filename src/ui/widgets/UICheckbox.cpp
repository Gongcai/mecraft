#include "UICheckbox.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <glad/glad.h>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include "../../renderer/core/Shader.h"
#include "../../resource/ResourceMgr.h"
#include "../core/UIRenderUtils.h"

namespace {
void pushThickSegment(std::vector<float>& buf,
                      float x0, float y0,
                      float x1, float y1,
                      float thickness) {
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len <= 0.001f) return;

    const float nx = -dy / len * thickness * 0.5f;
    const float ny = dx / len * thickness * 0.5f;
    buf.push_back(x0 + nx); buf.push_back(y0 + ny);
    buf.push_back(x1 + nx); buf.push_back(y1 + ny);
    buf.push_back(x1 - nx); buf.push_back(y1 - ny);
    buf.push_back(x0 + nx); buf.push_back(y0 + ny);
    buf.push_back(x1 - nx); buf.push_back(y1 - ny);
    buf.push_back(x0 - nx); buf.push_back(y0 - ny);
}
} // namespace

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

void UICheckbox::setStyle(const UICheckboxStyle& style) {
    m_localStyle = style;
    m_hasLocalStyle = true;
}

void UICheckbox::clearLocalStyle() {
    m_hasLocalStyle = false;
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
    // Box (6) + border (24) + check mark strokes (12) = 42 verts * 2 floats
    glBufferData(GL_ARRAY_BUFFER, 42 * 2 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
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

    const UIResolvedCheckboxStyle resolved =
        UIStyleResolver::resolveCheckbox(resolveBaseStyle(ctx), currentStyleState());
    float boxSize = resolved.boxSize;
    const auto& boxCol = resolved.box;
    const auto& borderCol = resolved.border;
    const auto& checkCol = resolved.check;
    const auto& textCol = resolved.text;

    float ax = getAbsoluteX(ctx);
    float ay = getAbsoluteY(ctx);
    float ah = height * scaleY;
    float cy = ay + ah * 0.5f;

    float bx0 = ax;
    float by0 = cy - boxSize * 0.5f;
    float bx1 = ax + boxSize;
    float by1 = cy + boxSize * 0.5f;
    float bw = resolved.borderWidth;

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

    // Check mark, scaled from the center.
    if (m_checked) {
        float scale = m_checkScaleTween.value();
        if (scale > 0.01f) {
            float icx = (bx0 + bx1) * 0.5f;
            float icy = (by0 + by1) * 0.5f;

            auto sx = [&](float localX) { return icx + (localX - icx) * scale; };
            auto sy = [&](float localY) { return icy + (localY - icy) * scale; };
            const float x0 = bx0 + boxSize * 0.25f;
            const float y0 = by0 + boxSize * 0.52f;
            const float x1 = bx0 + boxSize * 0.42f;
            const float y1 = by0 + boxSize * 0.34f;
            const float x2 = bx0 + boxSize * 0.76f;
            const float y2 = by0 + boxSize * 0.70f;
            const float thickness = std::max(2.0f, boxSize * 0.14f) * scale;

            std::array<float, 4> c = checkCol;
            c[3] *= alpha;
            m_shader->setVec4("uColor", glm::vec4(c[0], c[1], c[2], c[3]));
            std::vector<float> verts;
            verts.reserve(24);
            pushThickSegment(verts, sx(x0), sy(y0), sx(x1), sy(y1), thickness);
            pushThickSegment(verts, sx(x1), sy(y1), sx(x2), sy(y2), thickness);
            glBufferSubData(GL_ARRAY_BUFFER, 30 * 2 * sizeof(float),
                            static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
                            verts.data());
            glDrawArrays(GL_TRIANGLES, 30, static_cast<GLsizei>(verts.size() / 2));
        }
    }

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // Render label text to the right of the box
    float labelX = bx1 + 8.0f;
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

UICheckboxStyle UICheckbox::resolveBaseStyle(const UIRenderContext& ctx) const {
    if (m_hasLocalStyle) {
        return m_localStyle;
    }

    UICheckboxStyle style = UIStyleResolver::checkboxStyleFromTheme(ctx.theme);
    if (!ctx.theme) {
        style.boxNormal = m_boxColor;
        style.boxHover = m_boxHoverColor;
        style.boxDisabled = m_boxColor;
        style.borderNormal = m_boxBorderColor;
        style.borderDisabled = m_boxBorderColor;
        style.check = m_checkColor;
    }
    return style;
}

int UICheckbox::currentStyleState() const {
    if (!interactive) {
        return static_cast<int>(UIStyleState_Disabled);
    }

    int state = static_cast<int>(UIStyleState_Normal);
    if (m_hovered || isFocused()) {
        state |= static_cast<int>(UIStyleState_Hovered);
    }
    return state;
}
