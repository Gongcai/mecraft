#include "UIToggle.h"

#include <glad/glad.h>
#include <cmath>

#include "../core/UIRenderUtils.h"
#include "../core/UITheme.h"
#include "../../resource/ResourceMgr.h"

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

namespace {
// Generate vertices for a filled circle approximated by a triangle fan.
void pushCircle(std::vector<float>& buf, float cx, float cy, float radius, int segments) {
    for (int i = 0; i < segments; ++i) {
        const float a0 = static_cast<float>(i) * 2.0f * 3.14159265f / static_cast<float>(segments);
        const float a1 = static_cast<float>(i + 1) * 2.0f * 3.14159265f / static_cast<float>(segments);
        buf.push_back(cx); buf.push_back(cy);
        buf.push_back(cx + std::cos(a0) * radius); buf.push_back(cy + std::sin(a0) * radius);
        buf.push_back(cx + std::cos(a1) * radius); buf.push_back(cy + std::sin(a1) * radius);
    }
}
} // namespace

UIToggle::UIToggle() {
    interactive = true;
    focusable = true;
    width = 44.0f;
    height = 22.0f;
}

UIToggle::~UIToggle() {
    shutdown();
}

void UIToggle::init(ResourceMgr& resourceMgr) {
    m_shader = resourceMgr.getShader("ui_color");
    initMesh();
    m_knobTween.start(m_checked ? 1.0f : 0.0f, m_checked ? 1.0f : 0.0f, 0.15f, EasingType::EaseOut);
    m_label.init(resourceMgr);
    UIWidget::init(resourceMgr);
}

void UIToggle::shutdown() {
    m_label.shutdown();
    cleanupMesh();
    m_shader = nullptr;
    UIWidget::shutdown();
}

void UIToggle::setChecked(bool checked) {
    if (m_checked == checked) return;
    m_checked = checked;
    m_knobTween.start(m_knobTween.value(), m_checked ? 1.0f : 0.0f, 0.15f, EasingType::EaseOut);
}

void UIToggle::setLabel(const std::string& text) {
    m_label.setText(text);
}

void UIToggle::updateAnimations(float dt) {
    m_knobTween.tick(dt);
    UIWidget::updateAnimations(dt);
}

void UIToggle::setFocused(bool focused) {
    UIWidget::setFocused(focused);
}

void UIToggle::toggle() {
    m_checked = !m_checked;
    m_knobTween.start(m_knobTween.value(), m_checked ? 1.0f : 0.0f, 0.15f, EasingType::EaseOut);
    if (onChanged) onChanged(m_checked);
}

void UIToggle::initMesh() {
    // Track rect (6 verts) + knob circle (12 segments * 3 verts * 2 floats = 72 floats)
    // Total: 6*2 + 72 = 84 floats for track, same structure for knob = track(12) + knob(72)
    // We'll use dynamic buffer sized for track + circle.
    constexpr int circleSegments = 12;
    constexpr int trackFloats = 6 * 2;
    constexpr int knobFloats = circleSegments * 3 * 2;
    constexpr int totalFloats = trackFloats + knobFloats;

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(totalFloats * sizeof(float)),
                 nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);
}

void UIToggle::cleanupMesh() {
    if (m_vao) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    if (m_vbo) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
}

void UIToggle::renderSelf(const UIRenderContext& ctx) const {
    if (!m_shader) return;

    const UIRenderUtils::GLStateGuard guard;

    const Color trackOff = (!m_hasLocalColors && ctx.theme) ? ctx.theme->toggleTrackOff : m_trackOffColor;
    const Color trackOn  = (!m_hasLocalColors && ctx.theme) ? ctx.theme->toggleTrackOn  : m_trackOnColor;
    const Color knobNorm = (!m_hasLocalColors && ctx.theme) ? ctx.theme->toggleKnob     : m_knobColor;
    const Color knobHov  = (!m_hasLocalColors && ctx.theme) ? ctx.theme->toggleKnobHover: m_knobHoverColor;

    const float toggleW = (ctx.theme && !m_hasLocalColors) ? ctx.theme->toggleWidth  : width;
    const float toggleH = (ctx.theme && !m_hasLocalColors) ? ctx.theme->toggleHeight : height;

    const float ax = getAbsoluteX(ctx);
    const float ay = getAbsoluteY(ctx);

    const float trackRadius = toggleH * 0.5f;
    const float knobRadius = toggleH * 0.38f;
    const float t = m_knobTween.value();
    const float knobCx = ax + trackRadius + t * (toggleW - toggleH);
    const float knobCy = ay + trackRadius;

    // Interpolate track color.
    const Color trackCol = {
        trackOff[0] + (trackOn[0] - trackOff[0]) * t,
        trackOff[1] + (trackOn[1] - trackOff[1]) * t,
        trackOff[2] + (trackOn[2] - trackOff[2]) * t,
        trackOff[3] + (trackOn[3] - trackOff[3]) * t,
    };

    const Color knobCol = m_hovered ? knobHov : knobNorm;

    // Build vertices.
    std::vector<float> verts;
    verts.reserve(12 + 12 * 3 * 2);
    // Track rectangle (rounded look via full rect; the circle ends give rounded appearance).
    UIRenderUtils::pushColorQuad(verts, ax, ay, ax + toggleW, ay + toggleH);
    // Knob circle.
    constexpr int circleSegments = 12;
    pushCircle(verts, knobCx, knobCy, knobRadius, circleSegments);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    static_cast<GLsizeiptr>(verts.size() * sizeof(float)), verts.data());

    m_shader->use();
    m_shader->setVec2("uScreenSize",
                      glm::vec2(static_cast<float>(ctx.screenWidth),
                                static_cast<float>(ctx.screenHeight)));

    // Draw track.
    m_shader->setVec4("uColor", glm::vec4(trackCol[0], trackCol[1], trackCol[2], trackCol[3] * alpha));
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // Draw knob.
    m_shader->setVec4("uColor", glm::vec4(knobCol[0], knobCol[1], knobCol[2], knobCol[3] * alpha));
    glDrawArrays(GL_TRIANGLES, 6, circleSegments * 3);

    glBindVertexArray(0);

    // Render label to the right.
    const float labelGap = 8.0f;
    m_label.x = x + toggleW + labelGap;
    m_label.y = y + (toggleH - m_label.height) * 0.5f;
    m_label.render(ctx);
}

UIEventResult UIToggle::onInput(const UIInputEvent& event, const UIRenderContext& ctx) {
    if (!visible || !interactive) return UIEventResult::Ignored;

    // Forward to children first.
    const UIEventResult childResult = UIWidget::onInput(event, ctx);
    if (childResult == UIEventResult::Consumed) return UIEventResult::Consumed;

    const bool inside = hitTest(event.x, event.y, ctx);

    switch (event.type) {
    case UIInputEventType::PointerMove:
        m_hovered = inside;
        return inside ? UIEventResult::Handled : UIEventResult::Ignored;

    case UIInputEventType::PointerDown:
        if (event.button == UIPointerButton::Primary && inside) {
            return UIEventResult::Handled;
        }
        break;

    case UIInputEventType::PointerUp:
        if (event.button == UIPointerButton::Primary && inside) {
            toggle();
            return UIEventResult::Consumed;
        }
        break;

    case UIInputEventType::Command:
        if (isFocused() && event.command == UICommand::Activate) {
            toggle();
            return UIEventResult::Consumed;
        }
        break;

    default:
        break;
    }

    return childResult;
}
