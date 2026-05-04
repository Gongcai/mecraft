#include "MainMenuScreen.h"

#include "../UIButton.h"
#include "../UIText.h"
#include "../UIPanel.h"

void MainMenuScreen::buildUI(ResourceMgr& resourceMgr) {
    // Title text "MECRAFT"
    auto title = std::make_unique<UIText>();
    title->setText("MECRAFT");
    title->setTextScale(6.0f);
    title->setTextColor({0.2f, 0.8f, 1.0f, 1.0f});
    title->anchor = Anchor::Center;
    title->anchorOffsetY = 80.0f;
    title->width = title->measureTextWidth();
    title->height = title->measureTextHeight();
    m_title = title.get();

    // Start button
    auto startBtn = std::make_unique<UIButton>();
    startBtn->init(resourceMgr);
    startBtn->setText("START GAME");
    startBtn->setTextScale(2.0f);
    startBtn->width = 280.0f;
    startBtn->height = 50.0f;
    startBtn->anchor = Anchor::Center;
    startBtn->anchorOffsetY = -20.0f;
    startBtn->setNormalColor({0.25f, 0.25f, 0.25f, 0.9f});
    startBtn->setHoverColor({0.4f, 0.4f, 0.4f, 1.0f});
    startBtn->setOnClick([this]() {
        if (onStartClicked) onStartClicked();
    });
    m_startButton = startBtn.get();

    // Quit button
    auto quitBtn = std::make_unique<UIButton>();
    quitBtn->init(resourceMgr);
    quitBtn->setText("QUIT");
    quitBtn->setTextScale(2.0f);
    quitBtn->width = 280.0f;
    quitBtn->height = 50.0f;
    quitBtn->anchor = Anchor::Center;
    quitBtn->anchorOffsetY = -80.0f;
    quitBtn->setNormalColor({0.25f, 0.25f, 0.25f, 0.9f});
    quitBtn->setHoverColor({0.4f, 0.4f, 0.4f, 1.0f});
    quitBtn->setOnClick([this]() {
        if (onQuitClicked) onQuitClicked();
    });
    m_quitButton = quitBtn.get();

    // Add root widgets
    addRoot(std::move(title));
    addRoot(std::move(startBtn));
    addRoot(std::move(quitBtn));

    // Register tweens
    registerFloatTween(m_titleY);
    registerFloatTween(m_titleGlow);
}

void MainMenuScreen::onSceneEnter() {
    // Title elastic drop: start from off-screen (above), drop to target
    m_titleY.start(400.0f, 80.0f, kTitleDropDuration, EasingType::ElasticOut);

    // Title glow breathing effect (ping-pong alpha)
    m_titleGlow.start(0.7f, 1.0f, kGlowPeriod, EasingType::EaseInOut);
    m_titleGlow.setPingPong(true);
}

void MainMenuScreen::updateAnimations(float dt) {
    UIScene::updateAnimations(dt);

    // Apply tween values to widgets
    if (m_title) {
        m_title->anchorOffsetY = m_titleY.value();
        m_title->alpha = m_titleGlow.value();
    }
}
