#include "UIRadioButton.h"

#include <glad/glad.h>
#include <cmath>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include "../core/UIRenderUtils.h"
#include "../core/UITheme.h"
#include "../font/TextRenderer.h"
#include "../../resource/ResourceMgr.h"

namespace {
constexpr int kCircleSegments = 12;
// Outer circle + inner circle = 2 * segments * 3 verts * 2 floats
constexpr int kBufferFloats = 2 * kCircleSegments * 3 * 2;

void pushCircle(std::vector<float>& buf, float cx, float cy, float radius) {
    for (int i = 0; i < kCircleSegments; ++i) {
        const float a0 = static_cast<float>(i) * 2.0f * 3.14159265f / static_cast<float>(kCircleSegments);
        const float a1 = static_cast<float>(i + 1) * 2.0f * 3.14159265f / static_cast<float>(kCircleSegments);
        buf.push_back(cx); buf.push_back(cy);
        buf.push_back(cx + std::cos(a0) * radius); buf.push_back(cy + std::sin(a0) * radius);
        buf.push_back(cx + std::cos(a1) * radius); buf.push_back(cy + std::sin(a1) * radius);
    }
}
} // namespace

UIRadioButtonGroup::UIRadioButtonGroup() {
    interactive = true;
    focusable = false; // The group itself is not focusable; individual hit-testing handles interaction.
    width = 200.0f;
    height = 100.0f;
}

UIRadioButtonGroup::~UIRadioButtonGroup() {
    shutdown();
}

void UIRadioButtonGroup::init(ResourceMgr& resourceMgr) {
    m_shader = resourceMgr.getShader("ui_color");
    initMesh();
    UIWidget::init(resourceMgr);
}

void UIRadioButtonGroup::shutdown() {
    cleanupMesh();
    m_shader = nullptr;
    UIWidget::shutdown();
}

int UIRadioButtonGroup::addOption(const std::string& text) {
    Option opt;
    opt.text = text;
    opt.selectTween.start(0.0f, 0.0f, 0.15f, EasingType::EaseOut);
    m_options.push_back(std::move(opt));

    // Recalculate height.
    const float rowHeight = 24.0f;
    height = static_cast<float>(m_options.size()) * (rowHeight + m_spacing) - m_spacing;
    return static_cast<int>(m_options.size()) - 1;
}

void UIRadioButtonGroup::setSelectedIndex(int index) {
    if (index < 0 || index >= static_cast<int>(m_options.size())) return;
    if (m_selectedIndex == index) return;

    // Animate old selection out.
    if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_options.size())) {
        m_options[m_selectedIndex].selectTween.start(
            m_options[m_selectedIndex].selectTween.value(), 0.0f, 0.15f, EasingType::EaseOut);
    }

    m_selectedIndex = index;

    // Animate new selection in.
    m_options[m_selectedIndex].selectTween.start(
        m_options[m_selectedIndex].selectTween.value(), 1.0f, 0.15f, EasingType::EaseOut);

    if (onSelectionChanged) onSelectionChanged(m_selectedIndex);
}

void UIRadioButtonGroup::updateAnimations(float dt) {
    for (auto& opt : m_options) {
        opt.selectTween.tick(dt);
    }
    UIWidget::updateAnimations(dt);
}

void UIRadioButtonGroup::initMesh() {
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(kBufferFloats * sizeof(float)),
                 nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);
}

void UIRadioButtonGroup::cleanupMesh() {
    if (m_vao) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    if (m_vbo) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
}

int UIRadioButtonGroup::hitTestOption(float px, float py, const UIRenderContext& ctx) const {
    const float flippedY = static_cast<float>(ctx.screenHeight) - py;
    const float ax = getAbsoluteX(ctx);
    const float ay = getAbsoluteY(ctx);
    const float radioSz = ctx.theme ? ctx.theme->radioSize : 18.0f;
    const float rowHeight = 24.0f;

    for (int i = 0; i < static_cast<int>(m_options.size()); ++i) {
        const float rowY = ay + static_cast<float>(i) * (rowHeight + m_spacing);
        const float cy = rowY + rowHeight * 0.5f;
        // Hit test the radio circle area (with padding).
        const float dx = px - (ax + radioSz * 0.5f);
        const float dy = flippedY - cy;
        if (dx * dx + dy * dy <= (radioSz * 0.5f + 4.0f) * (radioSz * 0.5f + 4.0f)) {
            return i;
        }
        // Also allow clicking on the label text area.
        if (px >= ax && px < ax + width * scaleX &&
            flippedY >= rowY && flippedY < rowY + rowHeight) {
            return i;
        }
    }
    return -1;
}

void UIRadioButtonGroup::renderSelf(const UIRenderContext& ctx) const {
    if (!m_shader || m_options.empty()) return;

    const UIRenderUtils::GLStateGuard guard;

    const Color outerCol = (!m_hasLocalColors && ctx.theme) ? ctx.theme->radioOuter : m_outerColor;
    const Color outerHov = (!m_hasLocalColors && ctx.theme) ? ctx.theme->radioOuterHover : m_outerHoverColor;
    const Color innerCol = (!m_hasLocalColors && ctx.theme) ? ctx.theme->radioInner : m_innerColor;
    const Color txtCol   = (!m_hasLocalColors && ctx.theme) ? ctx.theme->textPrimary : m_textColor;
    const float radioSz  = (ctx.theme && !m_hasLocalColors) ? ctx.theme->radioSize : 18.0f;
    const float rowHeight = 24.0f;

    const float ax = getAbsoluteX(ctx);
    const float ay = getAbsoluteY(ctx);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

    m_shader->use();
    m_shader->setVec2("uScreenSize",
                      glm::vec2(static_cast<float>(ctx.screenWidth),
                                static_cast<float>(ctx.screenHeight)));

    for (int i = 0; i < static_cast<int>(m_options.size()); ++i) {
        const Option& opt = m_options[i];
        const float rowY = ay + static_cast<float>(i) * (rowHeight + m_spacing);
        const float cy = rowY + rowHeight * 0.5f;
        const float cx = ax + radioSz * 0.5f;
        const float outerR = radioSz * 0.5f;
        const float innerR = outerR * 0.55f * opt.selectTween.value();

        // Build outer circle.
        std::vector<float> verts;
        verts.reserve(kBufferFloats);
        pushCircle(verts, cx, cy, outerR);
        if (innerR > 0.5f) {
            pushCircle(verts, cx, cy, innerR);
        }

        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        static_cast<GLsizeiptr>(verts.size() * sizeof(float)), verts.data());

        // Draw outer circle.
        const Color oc = opt.hovered ? outerHov : outerCol;
        m_shader->setVec4("uColor", glm::vec4(oc[0], oc[1], oc[2], oc[3] * alpha));
        glDrawArrays(GL_TRIANGLES, 0, kCircleSegments * 3);

        // Draw inner circle.
        if (innerR > 0.5f) {
            m_shader->setVec4("uColor", glm::vec4(innerCol[0], innerCol[1], innerCol[2], innerCol[3] * alpha));
            glDrawArrays(GL_TRIANGLES, kCircleSegments * 3, kCircleSegments * 3);
        }
    }

    glBindVertexArray(0);

    // Render label text.
    if (ctx.textRenderer) {
        const float textScale = 1.0f;
        const float textX = ax + radioSz + 8.0f;
        for (int i = 0; i < static_cast<int>(m_options.size()); ++i) {
            const Option& opt = m_options[i];
            const float rowY = ay + static_cast<float>(i) * (rowHeight + m_spacing);
            const auto metrics = ctx.textRenderer->measureText(opt.text, textScale);
            const float textY = rowY + (rowHeight - metrics.height) * 0.5f;
            ctx.textRenderer->render(opt.text, textX, textY, textScale,
                                     {txtCol[0], txtCol[1], txtCol[2], txtCol[3] * alpha},
                                     static_cast<float>(ctx.screenWidth),
                                     static_cast<float>(ctx.screenHeight));
        }
    }
}

UIEventResult UIRadioButtonGroup::onInput(const UIInputEvent& event, const UIRenderContext& ctx) {
    if (!visible || !interactive) return UIEventResult::Ignored;

    switch (event.type) {
    case UIInputEventType::PointerMove: {
        const int idx = hitTestOption(event.x, event.y, ctx);
        bool changed = false;
        for (int i = 0; i < static_cast<int>(m_options.size()); ++i) {
            const bool wasHovered = m_options[i].hovered;
            m_options[i].hovered = (i == idx);
            if (m_options[i].hovered != wasHovered) changed = true;
        }
        return (idx >= 0 || changed) ? UIEventResult::Handled : UIEventResult::Ignored;
    }

    case UIInputEventType::PointerUp:
        if (event.button == UIPointerButton::Primary) {
            const int idx = hitTestOption(event.x, event.y, ctx);
            if (idx >= 0) {
                setSelectedIndex(idx);
                return UIEventResult::Consumed;
            }
        }
        break;

    default:
        break;
    }

    return UIEventResult::Ignored;
}
