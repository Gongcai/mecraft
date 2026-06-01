#include "UIModal.h"

#include <glad/glad.h>
#include <algorithm>

#include "../core/UIRenderUtils.h"
#include "../core/UITheme.h"
#include "../font/TextRenderer.h"
#include "../../resource/ResourceMgr.h"

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

void UIModal::init(ResourceMgr& resourceMgr) {
    m_shader = resourceMgr.getShader("ui_color");

    // Overlay panel (full screen dim).
    m_overlayPanel.setBackgroundColor({0.0f, 0.0f, 0.0f, 0.0f});
    m_overlayPanel.setUseLocalColors(true);
    m_overlayPanel.init(resourceMgr);
    m_overlayPanel.width = 9999.0f;
    m_overlayPanel.height = 9999.0f;
    m_overlayPanel.anchor = Anchor::BottomLeft;

    // Content panel.
    m_contentPanel.init(resourceMgr);
    m_contentPanel.width = kPanelWidth;
    m_contentPanel.height = kPanelMinHeight;
    m_contentPanel.anchor = Anchor::Center;

    // Title text.
    m_title.init(resourceMgr);
    m_title.setTextScale(2.0f);
    m_title.anchor = Anchor::TopCenter;
    m_title.anchorOffsetY = -kPadding;

    // Overlay alpha tween (starts hidden).
    m_overlayAlpha.start(0.0f, 0.0f, 0.3f, EasingType::EaseOut);
    m_panelScale.start(0.8f, 0.8f, 0.3f, EasingType::EaseOut);

    // Mesh for overlay (6 verts).
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(12 * sizeof(float)),
                 nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);

    UIWidget::init(resourceMgr);
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
    if (m_vao) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    if (m_vbo) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
    m_shader = nullptr;
    UIWidget::shutdown();
}

void UIModal::setTitle(const std::string& title) {
    m_title.setText(title);
}

int UIModal::addButton(const std::string& text, std::function<void()> onClick) {
    auto btn = std::make_unique<UIButton>();
    btn->setText(text);
    btn->width = 100.0f;
    btn->height = 32.0f;
    const int index = static_cast<int>(m_buttons.size());
    m_buttons.push_back(std::move(btn));
    m_buttonCallbacks.push_back(std::move(onClick));
    layoutButtons();
    return index;
}

void UIModal::show() {
    m_open = true;
    m_overlayAlpha.start(0.0f, 0.6f, 0.3f, EasingType::EaseOut);
    m_panelScale.start(0.8f, 1.0f, 0.3f, EasingType::EaseOut);
}

void UIModal::close() {
    m_open = false;
    m_overlayAlpha.start(m_overlayAlpha.value(), 0.0f, 0.2f, EasingType::EaseIn);
    m_panelScale.start(m_panelScale.value(), 0.8f, 0.2f, EasingType::EaseIn);
    if (onClose) onClose();
}

void UIModal::layoutButtons() {
    if (m_buttons.empty()) return;
    const float totalWidth = static_cast<float>(m_buttons.size()) * 100.0f +
                             static_cast<float>(m_buttons.size() - 1) * kButtonSpacing;
    float xOffset = (kPanelWidth - totalWidth) * 0.5f;
    for (auto& btn : m_buttons) {
        btn->anchor = Anchor::BottomLeft;
        btn->anchorOffsetX = xOffset;
        btn->anchorOffsetY = kPadding;
        btn->x = 0.0f;
        btn->y = 0.0f;
        xOffset += 100.0f + kButtonSpacing;
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
    if (!visible) return;

    const UIRenderUtils::GLStateGuard guard;

    // Update overlay panel color with animated alpha.
    Color overlayBg = ctx.theme ? ctx.theme->overlayDim : Color{0.0f, 0.0f, 0.0f, 0.6f};
    overlayBg[3] = m_overlayAlpha.value();
    const_cast<UIPanel&>(m_overlayPanel).setBackgroundColor(overlayBg);
    m_overlayPanel.render(ctx);

    if (!m_open && m_overlayAlpha.value() < 0.01f) return;

    // Render content panel with scale animation.
    const float panelW = kPanelWidth;
    const float panelH = kPanelMinHeight;
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

    // Apply theme colors.
    if (ctx.theme) {
        panel.setBackgroundColor(ctx.theme->panelBackground);
        panel.setBorderColor(ctx.theme->panelBorder);
        panel.setBorderWidth(ctx.theme->panelBorderWidth);
    }
    panel.render(ctx);

    // Render title.
    const_cast<UIText&>(m_title).anchor = Anchor::BottomLeft;
    const_cast<UIText&>(m_title).x = cx - panelW * 0.5f + kPadding;
    const_cast<UIText&>(m_title).y = cy + panelH * 0.5f - kTitleHeight;
    const_cast<UIText&>(m_title).width = panelW - 2.0f * kPadding;
    const_cast<UIText&>(m_title).height = kTitleHeight;
    if (ctx.theme && !m_title.hasLocalTextColor()) {
        const_cast<UIText&>(m_title).setTextColor(ctx.theme->textPrimary);
    }
    const_cast<UIText&>(m_title).setScale(scale);
    m_title.render(ctx);

    // Render buttons in a horizontal row at the bottom of the panel.
    if (!m_buttons.empty()) {
        const float btnW = 100.0f;
        const float btnH = 32.0f;
        const float totalBtnWidth = static_cast<float>(m_buttons.size()) * btnW +
                                    static_cast<float>(m_buttons.size() - 1) * kButtonSpacing;
        float btnX = cx - totalBtnWidth * 0.5f;
        const float btnY = cy - panelH * 0.5f + kPadding;
        for (auto& btn : const_cast<std::vector<std::unique_ptr<UIButton>>&>(m_buttons)) {
            btn->anchor = Anchor::BottomLeft;
            btn->x = btnX;
            btn->y = btnY;
            btn->width = btnW;
            btn->height = btnH;
            btn->setScale(scale);
            btn->render(ctx);
            btnX += btnW + kButtonSpacing;
        }
    }
}

UIEventResult UIModal::onInput(const UIInputEvent& event, const UIRenderContext& ctx) {
    if (!visible || !m_open) return UIEventResult::Ignored;

    // Hit-test buttons manually (onInput is protected in UIButton).
    if (event.type == UIInputEventType::PointerDown &&
        event.button == UIPointerButton::Primary && !m_buttons.empty()) {
        const float cx = static_cast<float>(ctx.screenWidth) * 0.5f;
        const float cy = static_cast<float>(ctx.screenHeight) * 0.5f;
        const float scale = m_panelScale.value();
        const float btnW = 100.0f * scale;
        const float btnH = 32.0f * scale;
        const float totalBtnWidth = static_cast<float>(m_buttons.size()) * btnW +
                                    static_cast<float>(m_buttons.size() - 1) * kButtonSpacing * scale;
        float btnX = cx - totalBtnWidth * 0.5f;
        const float btnY = cy - kPanelMinHeight * 0.5f * scale + kPadding * scale;
        const float flippedY = static_cast<float>(ctx.screenHeight) - event.y;
        for (size_t i = 0; i < m_buttons.size(); ++i) {
            if (event.x >= btnX && event.x < btnX + btnW &&
                flippedY >= btnY && flippedY < btnY + btnH) {
                if (i < m_buttonCallbacks.size() && m_buttonCallbacks[i]) {
                    m_buttonCallbacks[i]();
                }
                return UIEventResult::Consumed;
            }
            btnX += btnW + kButtonSpacing * scale;
        }
    }

    // Forward to content panel.
    const UIEventResult contentResult = m_contentPanel.onInput(event, ctx);
    if (contentResult == UIEventResult::Consumed) return UIEventResult::Consumed;

    // Handle overlay click to close.
    if (event.type == UIInputEventType::PointerDown &&
        event.button == UIPointerButton::Primary && m_closeOnOverlayClick) {
        // Check if click is outside the content panel.
        const float cx = static_cast<float>(ctx.screenWidth) * 0.5f;
        const float cy = static_cast<float>(ctx.screenHeight) * 0.5f;
        const float flippedY = static_cast<float>(ctx.screenHeight) - event.y;
        const float px = event.x;
        const float panelLeft = cx - kPanelWidth * 0.5f;
        const float panelRight = cx + kPanelWidth * 0.5f;
        const float panelBottom = cy - kPanelMinHeight * 0.5f;
        const float panelTop = cy + kPanelMinHeight * 0.5f;
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
