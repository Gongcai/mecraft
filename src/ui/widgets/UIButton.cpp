#include "UIButton.h"

#include <algorithm>

UIButton::UIButton() {
    interactive = true;
    focusable = true;
}

void UIButton::init(ResourceMgr& resourceMgr) {
    m_background.init(resourceMgr);
    m_background.setUseLocalColors(true);
    m_background.setBackgroundColor(m_normalColor);
    m_background.setBorderWidth(2.0f);
    m_background.setBorderColor({1.0f, 1.0f, 1.0f, 0.3f});
    m_background.anchor = Anchor::BottomLeft;

    m_label.clearLocalTextColor();
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

void UIButton::setTone(UIButtonTone tone) {
    m_tone = tone;
    m_hasLocalColors = false;
    m_hasLocalStyle = false;
}

void UIButton::setStyle(const UIComponentStyle& style) {
    m_localStyle = style;
    m_hasLocalStyle = true;
}

void UIButton::clearLocalStyle() {
    m_hasLocalStyle = false;
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
    const UIResolvedStyle resolved = UIStyleResolver::resolve(resolveBaseStyle(ctx), currentStyleState());

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
    const bool backgroundHasImmediateState = m_pressed || !interactive;
    const auto bgColor = (!backgroundHasImmediateState && m_hoverColorTween.isRunning()) ? m_hoverColorTween.value()
                                                                                         : resolved.background;
    const_cast<UIPanel&>(m_background).setBackgroundColor(bgColor);
    const_cast<UIPanel&>(m_background).setBorderColor(resolved.border);
    const_cast<UIPanel&>(m_background).setBorderWidth(resolved.borderWidth);
    const_cast<UIPanel&>(m_background).alpha = alpha;
    m_background.render(ctx);

    // Center text in button
    float tw = ctx.textRenderer ? m_label.measureTextWidth(*ctx.textRenderer) : 0.0f;
    float th = ctx.textRenderer ? m_label.measureTextHeight(*ctx.textRenderer) : 0.0f;
    const_cast<UIText&>(m_label).anchorOffsetX = cx - tw * 0.5f;
    const_cast<UIText&>(m_label).anchorOffsetY = cy - th * 0.5f;
    const bool textHadLocalColor = m_label.hasLocalTextColor();
    if (!textHadLocalColor) {
        const_cast<UIText&>(m_label).setTextColor(resolved.text);
    }
    const_cast<UIText&>(m_label).alpha = alpha;
    m_label.render(ctx);
    if (!textHadLocalColor) {
        const_cast<UIText&>(m_label).clearLocalTextColor();
    }
}

UIEventResult UIButton::onInput(const UIInputEvent& event, const UIRenderContext& ctx) {
    if (!visible || !interactive)
        return UIEventResult::Ignored;

    // Let children handle first
    UIEventResult childResult = UIWidget::onInput(event, ctx);
    if (childResult == UIEventResult::Consumed)
        return UIEventResult::Consumed;

    const UIComponentStyle baseStyle = resolveBaseStyle(ctx);

    bool inside = hitTest(event.x, event.y, ctx);

    switch (event.type) {
    case UIInputEventType::PointerMove: {
        if (inside && !m_hovered) {
            m_hovered = true;
            m_hoverScaleTween.start(1.0f, m_hoverTargetScale, m_hoverDuration, EasingType::EaseOut);
            m_hoverColorTween.start(baseStyle.backgroundNormal, baseStyle.backgroundHover, m_hoverDuration,
                                    EasingType::Linear);
        } else if (!inside && m_hovered) {
            m_hovered = false;
            m_hoverScaleTween.start(m_hoverTargetScale, 1.0f, m_hoverDuration, EasingType::EaseOut);
            m_hoverColorTween.start(baseStyle.backgroundHover, baseStyle.backgroundNormal, m_hoverDuration,
                                    EasingType::Linear);
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
            if (m_onClick)
                m_onClick();
            return UIEventResult::Consumed;
        }
        m_pressed = false;
        break;
    }
    case UIInputEventType::Command: {
        if (isFocused() && isActivateCommand(event)) {
            if (m_onClick)
                m_onClick();
            return UIEventResult::Consumed;
        }
        break;
    }
    case UIInputEventType::KeyDown:
    case UIInputEventType::KeyUp:
    case UIInputEventType::TextInput:
    case UIInputEventType::Scroll: break;
    }

    return UIEventResult::Ignored;
}

bool UIButton::isActivateCommand(const UIInputEvent& event) {
    return event.command == UICommand::Activate;
}

UIComponentStyle UIButton::resolveBaseStyle(const UIRenderContext& ctx) const {
    if (m_hasLocalStyle) {
        return m_localStyle;
    }

    UIComponentStyle style = UIStyleResolver::buttonStyleFromTheme(ctx.theme, m_tone);
    if (m_hasLocalColors) {
        style.backgroundNormal = m_normalColor;
        style.backgroundHover = m_hoverColor;
        style.backgroundPressed = m_pressedColor;
        style.borderNormal = {1.0f, 1.0f, 1.0f, 0.3f};
        style.borderHover = {1.0f, 1.0f, 1.0f, 0.3f};
        style.borderPressed = {1.0f, 1.0f, 1.0f, 0.3f};
        style.borderFocused = {1.0f, 1.0f, 1.0f, 0.45f};
        style.borderWidth = 2.0f;
    }
    return style;
}

int UIButton::currentStyleState() const {
    if (!interactive) {
        return static_cast<int>(UIStyleState_Disabled);
    }

    int state = static_cast<int>(UIStyleState_Normal);
    if (m_hovered || isFocused()) {
        state |= static_cast<int>(UIStyleState_Hovered);
    }
    if (m_pressed) {
        state |= static_cast<int>(UIStyleState_Pressed);
    }
    return state;
}
