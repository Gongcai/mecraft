#include "UIToggle.h"

#include <glad/glad.h>
#include <algorithm>
#include <vector>

#include "../core/UIRenderUtils.h"
#include "../../resource/ResourceMgr.h"

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

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
    m_label.anchor = Anchor::BottomLeft;
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

void UIToggle::setLabelTextScale(float scale) {
    m_label.setTextScale(scale);
}

void UIToggle::setStyle(const UIToggleStyle& style) {
    m_localStyle = style;
    m_hasLocalStyle = true;
}

void UIToggle::clearLocalStyle() {
    m_hasLocalStyle = false;
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
    constexpr int kMaxShapeVerts = 128;

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(kMaxShapeVerts * 2 * sizeof(float)),
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

    const UIResolvedToggleStyle resolved =
        UIStyleResolver::resolveToggle(resolveBaseStyle(ctx), currentStyleState());
    const Color trackOff = resolved.trackOff;
    const Color trackOn = resolved.trackOn;
    const Color knobCol = resolved.knob;
    const float toggleW = resolved.width;
    const float toggleH = resolved.height;

    const float ax = getAbsoluteX(ctx);
    const float widgetH = height * scaleY;
    const float ay = getAbsoluteY(ctx) + std::max(0.0f, (widgetH - toggleH) * 0.5f);

    const float trackRadius = toggleH * 0.5f;
    const float knobRadius = toggleH * 0.38f;
    const float t = m_knobTween.value();
    const float knobCx = ax + trackRadius + t * (toggleW - toggleH);
    const float knobCy = ay + trackRadius;

    // Interpolate track color.
    Color trackCol = {
        trackOff[0] + (trackOn[0] - trackOff[0]) * t,
        trackOff[1] + (trackOn[1] - trackOff[1]) * t,
        trackOff[2] + (trackOn[2] - trackOff[2]) * t,
        trackOff[3] + (trackOn[3] - trackOff[3]) * t,
    };
    const bool active = m_hovered || isFocused();
    if (active) {
        trackCol[0] = std::clamp(trackCol[0] * 0.82f, 0.0f, 1.0f);
        trackCol[1] = std::clamp(trackCol[1] * 0.82f, 0.0f, 1.0f);
        trackCol[2] = std::clamp(trackCol[2] * 0.82f, 0.0f, 1.0f);
        trackCol[3] = std::clamp(trackCol[3] * 1.08f, 0.0f, 1.0f);
    }

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

    m_shader->use();
    m_shader->setVec2("uScreenSize",
                      glm::vec2(static_cast<float>(ctx.screenWidth),
                                static_cast<float>(ctx.screenHeight)));

    auto drawShape = [&](const std::vector<float>& verts, Color shapeColor) {
        if (verts.empty()) return;
        shapeColor[3] *= alpha;
        m_shader->setVec4("uColor", glm::vec4(shapeColor[0], shapeColor[1], shapeColor[2], shapeColor[3]));
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
                        verts.data());
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size() / 2));
    };

    std::vector<float> verts;
    verts.reserve(128);

    verts.clear();
    UIRenderUtils::pushCapsule(verts, ax, ay, ax + toggleW, ay + toggleH);
    drawShape(verts, trackCol);

    verts.clear();
    Color knobShadow {0.0f, 0.0f, 0.0f, active ? 0.30f : 0.20f};
    UIRenderUtils::pushCircle(verts, knobCx, knobCy - 1.0f, knobRadius + 1.5f);
    drawShape(verts, knobShadow);

    verts.clear();
    UIRenderUtils::pushCircle(verts, knobCx, knobCy, knobRadius);
    drawShape(verts, knobCol);

    glBindVertexArray(0);

    // Render label to the right.
    const float labelGap = 8.0f;
    const Color textCol = resolved.text;
    const float th = ctx.textRenderer ? m_label.measureTextHeight(*ctx.textRenderer) : 0.0f;
    const_cast<UIText&>(m_label).anchorOffsetX = ax + toggleW + labelGap;
    const_cast<UIText&>(m_label).anchorOffsetY = ay + (toggleH - th) * 0.5f;
    const_cast<UIText&>(m_label).setTextColor(textCol);
    const_cast<UIText&>(m_label).alpha = alpha;
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

UIToggleStyle UIToggle::resolveBaseStyle(const UIRenderContext& ctx) const {
    if (m_hasLocalStyle) {
        return m_localStyle;
    }

    UIToggleStyle style = UIStyleResolver::toggleStyleFromTheme(ctx.theme);
    if (m_hasLocalColors || !ctx.theme) {
        style.trackOff = m_trackOffColor;
        style.trackOn = m_trackOnColor;
        style.trackDisabled = m_trackOffColor;
        style.knobNormal = m_knobColor;
        style.knobHover = m_knobHoverColor;
        style.knobDisabled = m_knobColor;
        style.width = width;
        style.height = height;
    }
    return style;
}

int UIToggle::currentStyleState() const {
    if (!interactive) {
        return static_cast<int>(UIStyleState_Disabled);
    }

    int state = static_cast<int>(UIStyleState_Normal);
    if (m_hovered || isFocused()) {
        state |= static_cast<int>(UIStyleState_Hovered);
    }
    return state;
}
