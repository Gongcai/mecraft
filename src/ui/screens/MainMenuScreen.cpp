#include "MainMenuScreen.h"

#include "../widgets/UIButton.h"
#include "../widgets/UIImage.h"
#include "../widgets/UIDropdown.h"
#include "../../Paths.h"
#include "../../resource/ResourceMgr.h"
#include "../../locale/LocaleManager.h"

void MainMenuScreen::buildUI(ResourceMgr& resourceMgr) {
    m_title = nullptr;
    m_startButton = nullptr;
    m_quitButton = nullptr;
    m_langDropdown = nullptr;
    m_langCodes.clear();

    // Title image
    auto title = std::make_unique<UIImage>();
    title->loadTexture(resourceMgr, "title", TITLE_TEXTURE_PATH);
    title->setScale(2.0f);
    title->anchor = Anchor::Center;
    title->anchorOffsetY = 80.0f;
    m_title = title.get();

    // Start button
    auto startBtn = std::make_unique<UIButton>();
    startBtn->setText(getLocaleManager() ? getLocaleManager()->tr("start_game") : "START GAME");
    startBtn->setTextScale(2.0f);
    startBtn->width = 280.0f;
    startBtn->height = 50.0f;
    startBtn->anchor = Anchor::Center;
    startBtn->anchorOffsetY = -20.0f;
    startBtn->setNormalColor({0.2f, 0.6f, 0.2f, 0.9f});
    startBtn->setHoverColor({0.3f, 0.8f, 0.3f, 1.0f});
    startBtn->setOnClick([this]() {
        if (onStartClicked) onStartClicked();
    });
    m_startButton = startBtn.get();

    // Quit button
    auto quitBtn = std::make_unique<UIButton>();
    quitBtn->setText(getLocaleManager() ? getLocaleManager()->tr("quit") : "QUIT");
    quitBtn->setTextScale(2.0f);
    quitBtn->width = 280.0f;
    quitBtn->height = 50.0f;
    quitBtn->anchor = Anchor::Center;
    quitBtn->anchorOffsetY = -80.0f;
    quitBtn->setNormalColor({0.4f, 0.2f, 0.2f, 0.9f});
    quitBtn->setHoverColor({0.6f, 0.3f, 0.3f, 1.0f});
    quitBtn->setOnClick([this]() {
        if (onQuitClicked) onQuitClicked();
    });
    m_quitButton = quitBtn.get();

    // Language dropdown
    auto langDropdown = std::make_unique<UIDropdown>();
    langDropdown->width = 180.0f;
    langDropdown->height = 30.0f;
    langDropdown->anchor = Anchor::BottomRight;
    langDropdown->anchorOffsetX = -10.0f;
    langDropdown->anchorOffsetY = 10.0f;
    if (getLocaleManager()) {
        auto availableLanguages = LocaleManager::getAvailableLanguages();
        if (availableLanguages.empty()) {
            availableLanguages.push_back(getLocaleManager()->getLanguage());
        }
        m_langCodes.swap(availableLanguages);
        std::vector<std::string> displayNames;
        displayNames.reserve(m_langCodes.size());
        int selectedIndex = 0;
        for (size_t i = 0; i < m_langCodes.size(); ++i) {
            displayNames.push_back(LocaleManager::getLanguageDisplayName(m_langCodes[i]));
            if (m_langCodes[i] == getLocaleManager()->getLanguage()) {
                selectedIndex = static_cast<int>(i);
            }
        }
        langDropdown->setOptions(std::move(displayNames));
        langDropdown->setSelectedIndex(selectedIndex);
    }
    langDropdown->setOnSelectionChanged([this](int idx, const std::string&) {
        if (getLocaleManager() && idx >= 0 && idx < static_cast<int>(m_langCodes.size())) {
            const_cast<LocaleManager*>(getLocaleManager())->setLanguage(m_langCodes[idx]);
            const_cast<LocaleManager*>(getLocaleManager())->saveSettings();
            refreshTexts();
        }
    });
    m_langDropdown = langDropdown.get();

    // Add root widgets
    addRoot(std::move(title));
    addRoot(std::move(startBtn));
    addRoot(std::move(quitBtn));
    addRoot(std::move(langDropdown));

    // Register tweens
    registerFloatTween(m_titleY);
    registerFloatTween(m_titleGlow);
}

void MainMenuScreen::refreshTexts() {
    if (!getLocaleManager()) return;
    // Title is an image, no text to refresh
    if (m_startButton) m_startButton->setText(getLocaleManager()->tr("start_game"));
    if (m_quitButton) m_quitButton->setText(getLocaleManager()->tr("quit"));
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
