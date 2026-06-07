#include "PauseMenuScreen.h"

#include "../widgets/UIPanel.h"
#include "../widgets/UIText.h"
#include "../widgets/UIButton.h"
#include "../layout/UIStackLayout.h"
#include "../../locale/LocaleManager.h"

#include <algorithm>

void PauseMenuScreen::buildUI(ResourceMgr& resourceMgr) {
    (void)resourceMgr;

    // Dark overlay covering the whole screen
    auto overlay = std::make_unique<UIPanel>();
    overlay->setTone(UIPanelTone::Overlay);
    overlay->alpha = 0.0f;
    overlay->anchor = Anchor::BottomLeft;
    overlay->x = 0.0f;
    overlay->y = 0.0f;
    overlay->width = 9999.0f; // Will be sized to screen in render
    overlay->height = 9999.0f;
    m_overlay = overlay.get();

    // "PAUSED" title
    auto title = std::make_unique<UIText>();
    title->setText(getLocaleManager() ? getLocaleManager()->tr("paused") : "PAUSED");
    title->setTextScale(4.0f);
    title->setTone(UITextTone::OnOverlay);
    title->setAlignment(TextAlignment::Center);
    title->anchor = Anchor::Center;
    title->anchorOffsetY = 120.0f;
    title->width = 0.0f;
    title->height = 0.0f;
    m_title = title.get();

    // Button stack
    auto stack = std::make_unique<UIStackLayout>();
    stack->setDirection(StackDirection::Vertical);
    stack->setSpacing(12.0f);
    stack->anchor = Anchor::Center;
    stack->anchorOffsetY = -20.0f;

    // Resume button
    auto resumeBtn = std::make_unique<UIButton>();
    resumeBtn->setText(getLocaleManager() ? getLocaleManager()->tr("resume") : "RESUME");
    resumeBtn->setTextScale(2.0f);
    resumeBtn->width = 250.0f;
    resumeBtn->height = 45.0f;
    resumeBtn->setTone(UIButtonTone::Success);
    resumeBtn->setOnClick([this]() {
        if (onResume) onResume();
    });
    m_resumeButton = resumeBtn.get();

    // Settings button
    auto settingsBtn = std::make_unique<UIButton>();
    settingsBtn->setText(getLocaleManager() ? getLocaleManager()->tr("settings") : "SETTINGS");
    settingsBtn->setTextScale(2.0f);
    settingsBtn->width = 250.0f;
    settingsBtn->height = 45.0f;
    settingsBtn->setTone(UIButtonTone::Primary);
    settingsBtn->setOnClick([this]() {
        if (onSettings) onSettings();
    });
    m_settingsButton = settingsBtn.get();

    // Quit to menu button
    auto quitBtn = std::make_unique<UIButton>();
    quitBtn->setText(getLocaleManager() ? getLocaleManager()->tr("quit_to_menu") : "QUIT TO MENU");
    quitBtn->setTextScale(2.0f);
    quitBtn->width = 250.0f;
    quitBtn->height = 45.0f;
    quitBtn->setTone(UIButtonTone::Danger);
    quitBtn->setOnClick([this]() {
        if (onQuitToMenu) onQuitToMenu();
    });
    m_quitButton = quitBtn.get();

    // Build stack
    stack->addChild(std::move(resumeBtn));
    stack->addChild(std::move(settingsBtn));
    stack->addChild(std::move(quitBtn));
    stack->layout();
    m_buttonStack = stack.get();

    // Add roots (overlay first so it's behind everything)
    addRoot(std::move(overlay));
    addRoot(std::move(title));
    addRoot(std::move(stack));

    // Register tweens
    registerFloatTween(m_overlayAlpha);
    registerFloatTween(m_buttonSlideX);
}

void PauseMenuScreen::layout(const UIRenderContext& ctx) {
    UIScene::layout(ctx);

    const float screenW = static_cast<float>(std::max(1, ctx.screenWidth));
    const float screenH = static_cast<float>(std::max(1, ctx.screenHeight));
    const float sideMargin = std::clamp(screenW * 0.04f, 28.0f, 64.0f);
    const float topMargin = std::clamp(screenH * 0.10f, 54.0f, 96.0f);
    const float titleH = 56.0f;
    const float titleGap = std::clamp(screenH * 0.07f, 44.0f, 72.0f);

    if (m_overlay) {
        m_overlay->anchor = Anchor::BottomLeft;
        m_overlay->x = 0.0f;
        m_overlay->y = 0.0f;
        m_overlay->anchorOffsetX = 0.0f;
        m_overlay->anchorOffsetY = 0.0f;
        m_overlay->width = screenW;
        m_overlay->height = screenH;
    }

    if (m_title) {
        m_title->anchor = Anchor::TopCenter;
        m_title->x = 0.0f;
        m_title->y = 0.0f;
        m_title->anchorOffsetX = 0.0f;
        m_title->anchorOffsetY = -topMargin;
        m_title->width = std::min(560.0f, std::max(240.0f, screenW - sideMargin * 2.0f));
        m_title->height = titleH;
    }

    if (!m_buttonStack) {
        return;
    }

    const float buttonW = std::clamp(screenW * 0.24f, 220.0f, 320.0f);
    const float buttonH = 45.0f;
    for (const auto& child : m_buttonStack->getChildren()) {
        child->width = buttonW;
        child->height = buttonH;
    }
    m_buttonStack->setSpacing(std::clamp(screenH * 0.017f, 10.0f, 16.0f));
    m_buttonStack->layout();

    const float titleBottom = screenH - topMargin - titleH;
    const float stackTopLimit = titleBottom - titleGap;
    const float stackCenterY = stackTopLimit - m_buttonStack->height * 0.5f;
    const float minCenterY = std::max(m_buttonStack->height * 0.5f + 36.0f, screenH * 0.30f);
    const float centerY = std::max(minCenterY, stackCenterY);

    m_buttonStack->anchor = Anchor::BottomCenter;
    m_buttonStack->anchorOffsetX = 0.0f;
    m_buttonStack->anchorOffsetY = centerY - m_buttonStack->height * 0.5f;
}

void PauseMenuScreen::onSceneEnter() {
    // Overlay fade in
    m_overlayAlpha.start(0.0f, 1.0f, kOverlayFadeDuration, EasingType::EaseOut);

    // Buttons slide in from the right
    m_buttonSlideX.start(200.0f, 0.0f, kButtonSlideDuration, EasingType::BackOut);
}

void PauseMenuScreen::onSceneExit() {
    m_overlayAlpha.start(1.0f, 0.0f, kOverlayFadeDuration * 0.5f, EasingType::EaseIn);
    m_buttonSlideX.start(0.0f, 200.0f, kButtonSlideDuration * 0.5f, EasingType::EaseIn);
}

void PauseMenuScreen::updateAnimations(float dt) {
    UIScene::updateAnimations(dt);

    if (m_overlay) {
        m_overlay->alpha = m_overlayAlpha.value();
    }
    if (m_buttonStack) {
        m_buttonStack->x = m_buttonSlideX.value();
    }
}
