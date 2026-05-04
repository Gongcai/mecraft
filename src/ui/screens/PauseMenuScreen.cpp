#include "PauseMenuScreen.h"

#include "../UIPanel.h"
#include "../UIText.h"
#include "../UIButton.h"
#include "../UIStackLayout.h"

void PauseMenuScreen::buildUI(ResourceMgr& resourceMgr) {
    // Dark overlay covering the whole screen
    auto overlay = std::make_unique<UIPanel>();
    overlay->init(resourceMgr);
    overlay->setBackgroundColor({0.0f, 0.0f, 0.0f, 0.0f}); // alpha animated
    overlay->anchor = Anchor::BottomLeft;
    overlay->x = 0.0f;
    overlay->y = 0.0f;
    overlay->width = 9999.0f; // Will be sized to screen in render
    overlay->height = 9999.0f;
    m_overlay = overlay.get();

    // "PAUSED" title
    auto title = std::make_unique<UIText>();
    title->setText("PAUSED");
    title->setTextScale(4.0f);
    title->setTextColor({1.0f, 1.0f, 1.0f, 1.0f});
    title->anchor = Anchor::Center;
    title->anchorOffsetY = 120.0f;
    title->width = title->measureTextWidth();
    title->height = title->measureTextHeight();
    m_title = title.get();

    // Button stack
    auto stack = std::make_unique<UIStackLayout>();
    stack->setDirection(StackDirection::Vertical);
    stack->setSpacing(12.0f);
    stack->anchor = Anchor::Center;
    stack->anchorOffsetY = -20.0f;

    // Resume button
    auto resumeBtn = std::make_unique<UIButton>();
    resumeBtn->init(resourceMgr);
    resumeBtn->setText("RESUME");
    resumeBtn->setTextScale(2.0f);
    resumeBtn->width = 250.0f;
    resumeBtn->height = 45.0f;
    resumeBtn->setNormalColor({0.2f, 0.4f, 0.2f, 0.9f});
    resumeBtn->setHoverColor({0.3f, 0.6f, 0.3f, 1.0f});
    resumeBtn->setOnClick([this]() {
        if (onResume) onResume();
    });
    m_resumeButton = resumeBtn.get();

    // Quit to menu button
    auto quitBtn = std::make_unique<UIButton>();
    quitBtn->init(resourceMgr);
    quitBtn->setText("QUIT TO MENU");
    quitBtn->setTextScale(2.0f);
    quitBtn->width = 250.0f;
    quitBtn->height = 45.0f;
    quitBtn->setNormalColor({0.4f, 0.2f, 0.2f, 0.9f});
    quitBtn->setHoverColor({0.6f, 0.3f, 0.3f, 1.0f});
    quitBtn->setOnClick([this]() {
        if (onQuitToMenu) onQuitToMenu();
    });
    m_quitButton = quitBtn.get();

    // Build stack
    stack->addChild(std::move(resumeBtn));
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

void PauseMenuScreen::onSceneEnter() {
    // Overlay fade in
    m_overlayAlpha.start(0.0f, 0.6f, kOverlayFadeDuration, EasingType::EaseOut);

    // Buttons slide in from the right
    m_buttonSlideX.start(200.0f, 0.0f, kButtonSlideDuration, EasingType::BackOut);
}

void PauseMenuScreen::onSceneExit() {
    m_overlayAlpha.start(0.6f, 0.0f, kOverlayFadeDuration * 0.5f, EasingType::EaseIn);
    m_buttonSlideX.start(0.0f, 200.0f, kButtonSlideDuration * 0.5f, EasingType::EaseIn);
}

void PauseMenuScreen::updateAnimations(float dt) {
    UIScene::updateAnimations(dt);

    if (m_overlay) {
        std::array<float, 4> bg = m_overlay->getBackgroundColor();
        bg[3] = m_overlayAlpha.value();
        m_overlay->setBackgroundColor(bg);
    }
    if (m_buttonStack) {
        m_buttonStack->x = m_buttonSlideX.value();
    }
}
