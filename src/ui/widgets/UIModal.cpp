#include "UIModal.h"

#include <algorithm>

#include "../font/TextRenderer.h"
#include "../../resource/GameResources.h"

UIModal::UIModal() {
    interactive = true;
    focusable = false;
    // The modal covers the full screen — actual size is set during render from context.
    width = 9999.0f;
    height = 9999.0f;
    anchor = Anchor::BottomLeft;
    anchorOffsetX = 0.0f;
    anchorOffsetY = 0.0f;
}

UIModal::~UIModal() {
    shutdown();
}

void UIModal::init(GameResources& resources, RhiDevice& rhiDevice) {
    const UIResolvedModalStyle style = fallbackStyle();

    // Overlay panel (full screen dim).
    m_overlayPanel.setBackgroundColor({0.0f, 0.0f, 0.0f, 0.0f});
    m_overlayPanel.setUseLocalColors(true);
    m_overlayPanel.init(resources, rhiDevice);
    m_overlayPanel.width = 9999.0f;
    m_overlayPanel.height = 9999.0f;
    m_overlayPanel.anchor = Anchor::BottomLeft;

    // Content panel.
    m_contentPanel.init(resources, rhiDevice);
    m_contentPanel.width = style.panelWidth;
    m_contentPanel.height = style.panelMinHeight;
    m_contentPanel.anchor = Anchor::Center;

    // Title text.
    m_title.init(resources, rhiDevice);
    m_title.setTextScale(style.titleTextScale);
    m_title.anchor = Anchor::TopCenter;
    m_title.anchorOffsetY = -style.padding;

    // Overlay alpha tween (starts hidden).
    m_overlayAlpha.start(0.0f, 0.0f, 0.3f, EasingType::EaseOut);
    m_panelScale.start(0.8f, 0.8f, 0.3f, EasingType::EaseOut);

    UIWidget::init(resources, rhiDevice);
}

void UIModal::shutdown() {
    m_overlayPanel.shutdown();
    m_contentPanel.shutdown();
    m_title.shutdown();
    for (auto& btn : m_buttons) {
        btn->shutdown();
    }
    m_buttons.clear();
    m_buttonCallbacks.clear();
    UIWidget::shutdown();
}

void UIModal::setTitle(const std::string& title) {
    m_title.setText(title);
}

void UIModal::setStyle(const UIModalStyle& style) {
    m_localStyle = style;
    m_hasLocalStyle = true;
    layoutButtons();
}

void UIModal::clearLocalStyle() {
    m_hasLocalStyle = false;
    layoutButtons();
}

int UIModal::addButton(const std::string& text, std::function<void()> onClick) {
    const UIResolvedModalStyle style = fallbackStyle();
    auto btn = std::make_unique<UIButton>();
    btn->setText(text);
    btn->width = style.buttonWidth;
    btn->height = style.buttonHeight;
    const int index = static_cast<int>(m_buttons.size());
    m_buttons.push_back(std::move(btn));
    m_buttonCallbacks.push_back(std::move(onClick));
    layoutButtons();
    return index;
}

void UIModal::show() {
    m_open = true;
    m_overlayAlpha.start(0.0f, fallbackStyle().overlay[3], 0.3f, EasingType::EaseOut);
    m_panelScale.start(0.8f, 1.0f, 0.3f, EasingType::EaseOut);
}

void UIModal::close() {
    m_open = false;
    m_overlayAlpha.start(m_overlayAlpha.value(), 0.0f, 0.2f, EasingType::EaseIn);
    m_panelScale.start(m_panelScale.value(), 0.8f, 0.2f, EasingType::EaseIn);
    if (onClose)
        onClose();
}

void UIModal::layoutButtons() {
    if (m_buttons.empty())
        return;
    const UIResolvedModalStyle style = fallbackStyle();
    const float totalWidth = static_cast<float>(m_buttons.size()) * style.buttonWidth +
                             static_cast<float>(m_buttons.size() - 1) * style.buttonSpacing;
    float xOffset = (style.panelWidth - totalWidth) * 0.5f;
    for (auto& btn : m_buttons) {
        btn->anchor = Anchor::BottomLeft;
        btn->anchorOffsetX = xOffset;
        btn->anchorOffsetY = style.padding;
        btn->x = 0.0f;
        btn->y = 0.0f;
        xOffset += style.buttonWidth + style.buttonSpacing;
    }
}

void UIModal::updateAnimations(float dt) {
    m_overlayAlpha.tick(dt);
    m_panelScale.tick(dt);
    for (auto& btn : m_buttons) {
        btn->updateAnimations(dt);
    }
    UIWidget::updateAnimations(dt);
}

void UIModal::render(const UIRenderContext& ctx) const {
    if (!visible)
        return;

    const UIResolvedModalStyle style = resolveStyle(ctx);

    // Update overlay panel color with animated alpha.
    Color overlayBg = style.overlay;
    overlayBg[3] = m_overlayAlpha.value();
    const_cast<UIPanel&>(m_overlayPanel).setBackgroundColor(overlayBg);
    m_overlayPanel.render(ctx);

    if (!m_open && m_overlayAlpha.value() < 0.01f)
        return;

    // Render content panel with scale animation.
    const float panelW = style.panelWidth;
    const float panelH = style.panelMinHeight;
    const float cx = static_cast<float>(ctx.screenWidth) * 0.5f;
    const float cy = static_cast<float>(ctx.screenHeight) * 0.5f;
    const float scale = m_panelScale.value();

    // Position content panel at center.
    auto& panel = const_cast<UIPanel&>(m_contentPanel);
    panel.anchor = Anchor::BottomLeft;
    panel.x = cx - panelW * 0.5f;
    panel.y = cy - panelH * 0.5f;
    panel.width = panelW;
    panel.height = panelH;
    panel.setScale(scale);

    panel.setBackgroundColor(style.panel.background);
    panel.setBorderColor(style.panel.border);
    panel.setBorderWidth(style.panel.borderWidth);
    panel.render(ctx);

    // Render title.
    const_cast<UIText&>(m_title).anchor = Anchor::BottomLeft;
    const_cast<UIText&>(m_title).x = cx - panelW * 0.5f + style.padding;
    const_cast<UIText&>(m_title).y = cy + panelH * 0.5f - style.titleHeight;
    const_cast<UIText&>(m_title).width = panelW - 2.0f * style.padding;
    const_cast<UIText&>(m_title).height = style.titleHeight;
    const_cast<UIText&>(m_title).setTextColor(style.titleText);
    const_cast<UIText&>(m_title).setTextScale(style.titleTextScale);
    const_cast<UIText&>(m_title).setScale(scale);
    m_title.render(ctx);

    // Render buttons in a horizontal row at the bottom of the panel.
    if (!m_buttons.empty()) {
        const float btnW = style.buttonWidth;
        const float btnH = style.buttonHeight;
        const float totalBtnWidth = static_cast<float>(m_buttons.size()) * btnW +
                                    static_cast<float>(m_buttons.size() - 1) * style.buttonSpacing;
        float btnX = cx - totalBtnWidth * 0.5f;
        const float btnY = cy - panelH * 0.5f + style.padding;
        for (auto& btn : const_cast<std::vector<std::unique_ptr<UIButton>>&>(m_buttons)) {
            btn->anchor = Anchor::BottomLeft;
            btn->x = btnX;
            btn->y = btnY;
            btn->width = btnW;
            btn->height = btnH;
            btn->setScale(scale);
            btn->render(ctx);
            btnX += btnW + style.buttonSpacing;
        }
    }
}

UIEventResult UIModal::onInput(const UIInputEvent& event, const UIRenderContext& ctx) {
    if (!visible || !m_open)
        return UIEventResult::Ignored;

    // Hit-test buttons manually (onInput is protected in UIButton).
    if (event.type == UIInputEventType::PointerDown && event.button == UIPointerButton::Primary && !m_buttons.empty()) {
        const UIResolvedModalStyle style = resolveStyle(ctx);
        const float cx = static_cast<float>(ctx.screenWidth) * 0.5f;
        const float cy = static_cast<float>(ctx.screenHeight) * 0.5f;
        const float scale = m_panelScale.value();
        const float btnW = style.buttonWidth * scale;
        const float btnH = style.buttonHeight * scale;
        const float totalBtnWidth = static_cast<float>(m_buttons.size()) * btnW +
                                    static_cast<float>(m_buttons.size() - 1) * style.buttonSpacing * scale;
        float btnX = cx - totalBtnWidth * 0.5f;
        const float btnY = cy - style.panelMinHeight * 0.5f * scale + style.padding * scale;
        const float flippedY = static_cast<float>(ctx.screenHeight) - event.y;
        for (size_t i = 0; i < m_buttons.size(); ++i) {
            if (event.x >= btnX && event.x < btnX + btnW && flippedY >= btnY && flippedY < btnY + btnH) {
                if (i < m_buttonCallbacks.size() && m_buttonCallbacks[i]) {
                    m_buttonCallbacks[i]();
                }
                return UIEventResult::Consumed;
            }
            btnX += btnW + style.buttonSpacing * scale;
        }
    }

    // Forward to content panel.
    const UIEventResult contentResult = m_contentPanel.onInput(event, ctx);
    if (contentResult == UIEventResult::Consumed)
        return UIEventResult::Consumed;

    // Handle overlay click to close.
    if (event.type == UIInputEventType::PointerDown && event.button == UIPointerButton::Primary &&
        m_closeOnOverlayClick) {
        // Check if click is outside the content panel.
        const UIResolvedModalStyle style = resolveStyle(ctx);
        const float cx = static_cast<float>(ctx.screenWidth) * 0.5f;
        const float cy = static_cast<float>(ctx.screenHeight) * 0.5f;
        const float flippedY = static_cast<float>(ctx.screenHeight) - event.y;
        const float px = event.x;
        const float panelLeft = cx - style.panelWidth * 0.5f;
        const float panelRight = cx + style.panelWidth * 0.5f;
        const float panelBottom = cy - style.panelMinHeight * 0.5f;
        const float panelTop = cy + style.panelMinHeight * 0.5f;
        if (px < panelLeft || px >= panelRight || flippedY < panelBottom || flippedY >= panelTop) {
            close();
            return UIEventResult::Consumed;
        }
    }

    // Cancel command closes the modal.
    if (event.type == UIInputEventType::Command && event.command == UICommand::Cancel) {
        close();
        return UIEventResult::Consumed;
    }

    // Consume all events while the modal is open (modal behavior).
    return UIEventResult::Consumed;
}

UIModalStyle UIModal::resolveBaseStyle(const UIRenderContext& ctx) const {
    if (m_hasLocalStyle) {
        return m_localStyle;
    }
    return UIStyleResolver::modalStyleFromTheme(ctx.theme);
}

UIResolvedModalStyle UIModal::resolveStyle(const UIRenderContext& ctx) const {
    return UIStyleResolver::resolveModal(resolveBaseStyle(ctx));
}

UIResolvedModalStyle UIModal::fallbackStyle() const {
    return UIStyleResolver::resolveModal(m_hasLocalStyle ? m_localStyle : UIModalStyle{});
}
