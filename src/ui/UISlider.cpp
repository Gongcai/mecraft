#include "UISlider.h"

#include <algorithm>
#include <cmath>

#include <glad/glad.h>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include "../renderer/Shader.h"
#include "../resource/ResourceMgr.h"
#include "UIRenderUtils.h"

UISlider::UISlider() {
    interactive = true;
    focusable = true;
    height = 24.0f;
    width = 200.0f;
}

UISlider::~UISlider() { shutdown(); }

void UISlider::init(ResourceMgr& resourceMgr) {
    m_shader = resourceMgr.getShader("ui_color");
    initMesh();
    m_handleScaleTween.setImmediate(1.0f);
}

void UISlider::shutdown() {
    cleanupMesh();
    m_shader = nullptr;
}

void UISlider::setRange(float min, float max) {
    m_min = min;
    m_max = max;
    m_value = std::clamp(m_value, m_min, m_max);
}

void UISlider::setValue(float value) {
    m_value = std::clamp(value, m_min, m_max);
}

float UISlider::getValue() const {
    return m_value;
}

void UISlider::setStep(float step) {
    m_step = step;
}

void UISlider::setOnValueChanged(std::function<void(float)> callback) {
    m_onValueChanged = std::move(callback);
}

void UISlider::updateAnimations(float dt) {
    m_handleScaleTween.tick(dt);
    UIWidget::updateAnimations(dt);
}

float UISlider::valueToNormalized(float val) const {
    if (m_max <= m_min) return 0.0f;
    return (val - m_min) / (m_max - m_min);
}

float UISlider::normalizedToValue(float norm) const {
    return m_min + norm * (m_max - m_min);
}

float UISlider::trackLeft(const UIRenderContext& ctx) const {
    const UITheme* theme = ctx.theme;
    float handleSize = theme ? theme->sliderHandleSize : 14.0f;
    return getAbsoluteX(ctx) + handleSize * 0.5f;
}

float UISlider::trackRight(const UIRenderContext& ctx) const {
    const UITheme* theme = ctx.theme;
    float handleSize = theme ? theme->sliderHandleSize : 14.0f;
    return getAbsoluteX(ctx) + width * scaleX - handleSize * 0.5f;
}

float UISlider::handleScreenX(const UIRenderContext& ctx) const {
    float tl = trackLeft(ctx);
    float tr = trackRight(ctx);
    return tl + valueToNormalized(m_value) * (tr - tl);
}

float UISlider::pointerToValue(float px, const UIRenderContext& ctx) const {
    float tl = trackLeft(ctx);
    float tr = trackRight(ctx);
    if (tr <= tl) return m_min;
    float norm = std::clamp((px - tl) / (tr - tl), 0.0f, 1.0f);
    return normalizedToValue(norm);
}

void UISlider::applyStep() {
    if (m_step > 0.0f) {
        m_value = std::round(m_value / m_step) * m_step;
        m_value = std::clamp(m_value, m_min, m_max);
    }
}

void UISlider::initMesh() {
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    // Track (6) + Fill (6) + Handle (6) = 18 verts * 2 floats
    glBufferData(GL_ARRAY_BUFFER, 18 * 2 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void UISlider::cleanupMesh() {
    if (m_vao != 0) {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    if (m_vbo != 0) {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
}

void UISlider::renderSelf(const UIRenderContext& ctx) const {
    if (!m_shader || m_vao == 0) return;

    const UITheme* theme = ctx.theme;
    float trackH = theme ? theme->sliderTrackHeight : 4.0f;
    float handleSize = theme ? theme->sliderHandleSize : 14.0f;

    const auto& trackCol = theme ? theme->sliderTrack : m_trackColor;
    const auto& fillCol = theme ? theme->sliderFill : m_fillColor;
    const auto& handleCol = (m_handleHovered && theme) ? theme->sliderHandleHover
                            : (m_handleHovered ? m_handleHoverColor : (theme ? theme->sliderHandle : m_handleColor));

    float ax = getAbsoluteX(ctx);
    float ay = getAbsoluteY(ctx);
    float aw = width * scaleX;
    float ah = height * scaleY;

    float cy = ay + ah * 0.5f;
    float tl = trackLeft(ctx);
    float tr = trackRight(ctx);
    float hx = handleScreenX(ctx);

    float handleScale = m_handleScaleTween.value();
    float hs = handleSize * handleScale;

    const UIRenderUtils::GLStateGuard glState;
    m_shader->use();
    m_shader->setVec2("uScreenSize", glm::vec2(static_cast<float>(ctx.screenWidth),
                                                static_cast<float>(ctx.screenHeight)));

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

    // Track quad
    std::array<float, 4> tc = trackCol;
    tc[3] *= alpha;
    m_shader->setVec4("uColor", glm::vec4(tc[0], tc[1], tc[2], tc[3]));
    float trackVerts[] = {
        tl, cy - trackH * 0.5f,  tr, cy - trackH * 0.5f,  tr, cy + trackH * 0.5f,
        tl, cy - trackH * 0.5f,  tr, cy + trackH * 0.5f,  tl, cy + trackH * 0.5f,
    };
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(trackVerts), trackVerts);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // Fill quad
    if (hx > tl) {
        std::array<float, 4> fc = fillCol;
        fc[3] *= alpha;
        m_shader->setVec4("uColor", glm::vec4(fc[0], fc[1], fc[2], fc[3]));
        float fillVerts[] = {
            tl, cy - trackH * 0.5f,  hx, cy - trackH * 0.5f,  hx, cy + trackH * 0.5f,
            tl, cy - trackH * 0.5f,  hx, cy + trackH * 0.5f,  tl, cy + trackH * 0.5f,
        };
        glBufferSubData(GL_ARRAY_BUFFER, 12 * sizeof(float), sizeof(fillVerts), fillVerts);
        glDrawArrays(GL_TRIANGLES, 6, 6);
    }

    // Handle quad
    {
        std::array<float, 4> hc = handleCol;
        hc[3] *= alpha;
        m_shader->setVec4("uColor", glm::vec4(hc[0], hc[1], hc[2], hc[3]));
        float handleVerts[] = {
            hx - hs * 0.5f, cy - hs * 0.5f,  hx + hs * 0.5f, cy - hs * 0.5f,  hx + hs * 0.5f, cy + hs * 0.5f,
            hx - hs * 0.5f, cy - hs * 0.5f,  hx + hs * 0.5f, cy + hs * 0.5f,  hx - hs * 0.5f, cy + hs * 0.5f,
        };
        glBufferSubData(GL_ARRAY_BUFFER, 24 * sizeof(float), sizeof(handleVerts), handleVerts);
        glDrawArrays(GL_TRIANGLES, 12, 6);
    }

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

UIEventResult UISlider::onInput(const UIInputEvent& event, const UIRenderContext& ctx) {
    if (!visible || !interactive) return UIEventResult::Ignored;

    UIEventResult childResult = UIWidget::onInput(event, ctx);
    if (childResult == UIEventResult::Consumed) return UIEventResult::Consumed;

    const UITheme* theme = ctx.theme;
    float handleSize = theme ? theme->sliderHandleSize : 14.0f;
    float hx = handleScreenX(ctx);
    float ay = getAbsoluteY(ctx);
    float ah = height * scaleY;
    float cy = ay + ah * 0.5f;
    float padding = 4.0f;

    // Flip GLFW Y to widget coords
    float flippedY = static_cast<float>(ctx.screenHeight) - event.y;

    switch (event.type) {
        case UIInputEventType::PointerMove: {
            if (m_dragging) {
                float newVal = pointerToValue(event.x, ctx);
                if (newVal != m_value) {
                    m_value = newVal;
                    applyStep();
                    if (m_onValueChanged) m_onValueChanged(m_value);
                }
                return UIEventResult::Consumed;
            }
            // Check handle hover
            bool insideHandle = std::abs(event.x - hx) <= (handleSize * 0.5f + padding)
                                && std::abs(flippedY - cy) <= (handleSize * 0.5f + padding);
            if (insideHandle && !m_handleHovered) {
                m_handleHovered = true;
                m_handleScaleTween.start(1.0f, 1.2f, 0.1f, EasingType::EaseOut);
            } else if (!insideHandle && m_handleHovered) {
                m_handleHovered = false;
                m_handleScaleTween.start(1.2f, 1.0f, 0.1f, EasingType::EaseOut);
            }
            return insideHandle ? UIEventResult::Handled : UIEventResult::Ignored;
        }
        case UIInputEventType::PointerDown: {
            if (event.button == UIPointerButton::Primary) {
                bool insideWidget = hitTest(event.x, event.y, ctx);
                if (insideWidget) {
                    m_dragging = true;
                    m_value = pointerToValue(event.x, ctx);
                    applyStep();
                    if (m_onValueChanged) m_onValueChanged(m_value);
                    return UIEventResult::Consumed;
                }
            }
            break;
        }
        case UIInputEventType::PointerUp: {
            if (event.button == UIPointerButton::Primary && m_dragging) {
                m_dragging = false;
                return UIEventResult::Consumed;
            }
            break;
        }
        case UIInputEventType::Command: {
            if (isFocused()) {
                float stepVal = m_step > 0.0f ? m_step : (m_max - m_min) * 0.05f;
                if (event.command == UICommand::NavigateLeft || event.command == UICommand::NavigateDown) {
                    m_value = std::max(m_min, m_value - stepVal);
                    applyStep();
                    if (m_onValueChanged) m_onValueChanged(m_value);
                    return UIEventResult::Consumed;
                }
                if (event.command == UICommand::NavigateRight || event.command == UICommand::NavigateUp) {
                    m_value = std::min(m_max, m_value + stepVal);
                    applyStep();
                    if (m_onValueChanged) m_onValueChanged(m_value);
                    return UIEventResult::Consumed;
                }
                if (event.command == UICommand::Home) {
                    m_value = m_min;
                    if (m_onValueChanged) m_onValueChanged(m_value);
                    return UIEventResult::Consumed;
                }
                if (event.command == UICommand::End) {
                    m_value = m_max;
                    if (m_onValueChanged) m_onValueChanged(m_value);
                    return UIEventResult::Consumed;
                }
            }
            break;
        }
        case UIInputEventType::Scroll: {
            if (hitTest(event.x, event.y, ctx) || isFocused()) {
                float stepVal = m_step > 0.0f ? m_step : (m_max - m_min) * 0.05f;
                if (event.scrollY > 0) {
                    m_value = std::min(m_max, m_value + stepVal);
                } else if (event.scrollY < 0) {
                    m_value = std::max(m_min, m_value - stepVal);
                }
                applyStep();
                if (m_onValueChanged) m_onValueChanged(m_value);
                return UIEventResult::Handled;
            }
            break;
        }
        default:
            break;
    }

    return UIEventResult::Ignored;
}
