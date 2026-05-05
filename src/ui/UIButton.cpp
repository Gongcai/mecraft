#include "UIButton.h"

#include <algorithm>

#include <GLFW/glfw3.h>

UIButton::UIButton() {
    interactive = true;
    focusable = true;
}

void UIButton::init(ResourceMgr& resourceMgr) {
    m_background.init(resourceMgr);
    m_background.setBackgroundColor(m_normalColor);
    m_background.setBorderWidth(2.0f);
    m_background.setBorderColor({1.0f, 1.0f, 1.0f, 0.3f});
    m_background.anchor = Anchor::BottomLeft;

    m_label.setTextColor({1.0f, 1.0f, 1.0f, 1.0f});
    m_label.anchor = Anchor::BottomLeft;

    // Initialize scale tween at 1.0
    m_hoverScaleTween.setImmediate(1.0f);
    m_hoverColorTween.setImmediate(m_normalColor);
}

void UIButton::shutdown() {
    m_background.shutdown();
    m_label.shutdown();
}

void UIButton::setText(const std::string& text) {
    m_label.setText(text);
}

void UIButton::setTextColor(const std::array<float, 4>& c) {
    m_label.setTextColor(c);
}

void UIButton::setTextScale(float scale) {
    m_label.setTextScale(scale);
}

void UIButton::updateAnimations(float dt) {
    m_hoverScaleTween.tick(dt);
    m_hoverColorTween.tick(dt);
    UIWidget::updateAnimations(dt);
}

void UIButton::renderSelf(const UIRenderContext& ctx) const {
    const bool highlighted = m_hovered || isFocused();

    // Apply hover scale from center
    float cx = getAbsoluteX(ctx) + width * 0.5f;
    float cy = getAbsoluteY(ctx) + height * 0.5f;
    float s = m_hoverScaleTween.value();
    if (highlighted) {
        s = std::max(s, m_hoverTargetScale);
    }
    float hw = width * s * 0.5f;
    float hh = height * s * 0.5f;

    // Position background via anchorOffset (getAbsoluteX/Y uses these, not x/y)
    const_cast<UIPanel&>(m_background).anchorOffsetX = cx - hw;
    const_cast<UIPanel&>(m_background).anchorOffsetY = cy - hh;
    const_cast<UIPanel&>(m_background).width = hw * 2.0f;
    const_cast<UIPanel&>(m_background).height = hh * 2.0f;
    const auto bgColor = highlighted ? m_hoverColor : m_hoverColorTween.value();
    const_cast<UIPanel&>(m_background).setBackgroundColor(bgColor);
    const_cast<UIPanel&>(m_background).alpha = alpha;
    m_background.render(ctx);

    // Center text in button
    float tw = m_label.measureTextWidth();
    float th = m_label.measureTextHeight();
    const_cast<UIText&>(m_label).anchorOffsetX = cx - tw * 0.5f;
    const_cast<UIText&>(m_label).anchorOffsetY = cy - th * 0.5f;
    const_cast<UIText&>(m_label).alpha = alpha;
    m_label.render(ctx);
}

UIEventResult UIButton::onInput(const UIInputEvent& event, const UIRenderContext& ctx) {
    if (!visible || !interactive) return UIEventResult::Ignored;

    // Let children handle first
    UIEventResult childResult = UIWidget::onInput(event, ctx);
    if (childResult == UIEventResult::Consumed) return UIEventResult::Consumed;

    bool inside = hitTest(event.x, event.y, ctx);

    switch (event.type) {
        case UIInputEventType::PointerMove: {
            if (inside && !m_hovered) {
                m_hovered = true;
                m_hoverScaleTween.start(1.0f, m_hoverTargetScale, m_hoverDuration, EasingType::EaseOut);
                m_hoverColorTween.start(m_normalColor, m_hoverColor, m_hoverDuration, EasingType::Linear);
            } else if (!inside && m_hovered) {
                m_hovered = false;
                m_hoverScaleTween.start(m_hoverTargetScale, 1.0f, m_hoverDuration, EasingType::EaseOut);
                m_hoverColorTween.start(m_hoverColor, m_normalColor, m_hoverDuration, EasingType::Linear);
            }
            return inside ? UIEventResult::Handled : UIEventResult::Ignored;
        }
        case UIInputEventType::PointerDown: {
            if (event.button == UIPointerButton::Primary && inside) {
                m_pressed = true;
                requestFocus();
                return UIEventResult::Handled;
            }
            break;
        }
        case UIInputEventType::PointerUp: {
            if (event.button == UIPointerButton::Primary && m_pressed && inside) {
                m_pressed = false;
                if (m_onClick) m_onClick();
                return UIEventResult::Consumed;
            }
            m_pressed = false;
            break;
        }
        case UIInputEventType::KeyDown: {
            if (isFocused() && isConfirmKey(event.key)) {
                m_pressed = true;
                return UIEventResult::Handled;
            }
            break;
        }
        case UIInputEventType::KeyUp: {
            if (isFocused() && isConfirmKey(event.key) && m_pressed) {
                m_pressed = false;
                if (m_onClick) m_onClick();
                return UIEventResult::Consumed;
            }
            break;
        }
        case UIInputEventType::TextInput:
        case UIInputEventType::Scroll:
            break;
    }

    return UIEventResult::Ignored;
}

bool UIButton::isConfirmKey(const int key) {
    return key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER || key == GLFW_KEY_SPACE;
}
