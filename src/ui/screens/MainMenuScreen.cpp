#include "MainMenuScreen.h"

#include "../widgets/UIButton.h"
#include "../widgets/UIImage.h"
#include "../widgets/UIDropdown.h"
#include "../widgets/UITextInput.h"
#include "../../Paths.h"
#include "../../resource/ResourceMgr.h"
#include "../../locale/LocaleManager.h"

void MainMenuScreen::buildUI(ResourceMgr& resourceMgr) {
    m_title = nullptr;
    m_startButton = nullptr;
    m_modelSceneButton = nullptr;
    m_multiplayerButton = nullptr;
    m_quitButton = nullptr;
    m_connectButton = nullptr;
    m_backButton = nullptr;
    m_addressInput = nullptr;
    m_langDropdown = nullptr;
    m_langCodes.clear();

    // Title image
    auto title = std::make_unique<UIImage>();
    title->loadTexture(resourceMgr, "title", TITLE_TEXTURE_PATH);
    title->setScale(2.0f);
    title->anchor = Anchor::Center;
    title->anchorOffsetY = 80.0f;
    m_title = title.get();

    // Start button (single-player)
    auto startBtn = std::make_unique<UIButton>();
    startBtn->setText(getLocaleManager() ? getLocaleManager()->tr("start_game") : "START GAME");
    startBtn->setTextScale(2.0f);
    startBtn->width = 300.0f;
    startBtn->height = 48.0f;
    startBtn->anchor = Anchor::Center;
    startBtn->anchorOffsetY = -20.0f;
    startBtn->setTone(UIButtonTone::Success);
    startBtn->setOnClick([this]() {
        if (onStartClicked)
            onStartClicked();
    });
    m_startButton = startBtn.get();

    auto modelSceneBtn = std::make_unique<UIButton>();
    modelSceneBtn->setText(getLocaleManager()->tr("model_scene"));
    modelSceneBtn->setTextScale(2.0f);
    modelSceneBtn->width = 300.0f;
    modelSceneBtn->height = 48.0f;
    modelSceneBtn->anchor = Anchor::Center;
    modelSceneBtn->anchorOffsetY = -80.0f;
    modelSceneBtn->setTone(UIButtonTone::Primary);
    modelSceneBtn->setOnClick([this]() {
        if (onModelSceneClicked)
            onModelSceneClicked();
    });
    m_modelSceneButton = modelSceneBtn.get();

    // Multiplayer button
    auto mpBtn = std::make_unique<UIButton>();
    mpBtn->setText(getLocaleManager() ? getLocaleManager()->tr("multiplayer") : "MULTIPLAYER");
    mpBtn->setTextScale(2.0f);
    mpBtn->width = 300.0f;
    mpBtn->height = 48.0f;
    mpBtn->anchor = Anchor::Center;
    mpBtn->anchorOffsetY = -140.0f;
    mpBtn->setTone(UIButtonTone::Primary);
    mpBtn->setOnClick([this]() { showMultiplayerPanel(); });
    m_multiplayerButton = mpBtn.get();

    // Quit button
    auto quitBtn = std::make_unique<UIButton>();
    quitBtn->setText(getLocaleManager() ? getLocaleManager()->tr("quit") : "QUIT");
    quitBtn->setTextScale(2.0f);
    quitBtn->width = 300.0f;
    quitBtn->height = 48.0f;
    quitBtn->anchor = Anchor::Center;
    quitBtn->anchorOffsetY = -200.0f;
    quitBtn->setTone(UIButtonTone::Danger);
    quitBtn->setOnClick([this]() {
        if (onQuitClicked)
            onQuitClicked();
    });
    m_quitButton = quitBtn.get();

    // --- Multiplayer panel (initially hidden) ---

    // Server address input
    auto addrInput = std::make_unique<UITextInput>();
    addrInput->width = 300.0f;
    addrInput->height = 40.0f;
    addrInput->anchor = Anchor::Center;
    addrInput->anchorOffsetY = -20.0f;
    addrInput->setPlaceholder("127.0.0.1:25565");
    addrInput->visible = false;
    m_addressInput = addrInput.get();

    // Connect button
    auto connectBtn = std::make_unique<UIButton>();
    connectBtn->setText("CONNECT");
    connectBtn->setTextScale(2.0f);
    connectBtn->width = 300.0f;
    connectBtn->height = 48.0f;
    connectBtn->anchor = Anchor::Center;
    connectBtn->anchorOffsetY = -80.0f;
    connectBtn->setTone(UIButtonTone::Success);
    connectBtn->visible = false;
    connectBtn->setOnClick([this]() {
        if (m_addressInput && onConnectClicked) {
            std::string addr = m_addressInput->getText();
            if (addr.empty())
                addr = "127.0.0.1";
            // Parse host:port
            int port = 25565;
            auto colonPos = addr.find(':');
            if (colonPos != std::string::npos) {
                port = std::atoi(addr.substr(colonPos + 1).c_str());
                if (port <= 0 || port > 65535) {
                    port = 25565;
                }
                addr = addr.substr(0, colonPos);
            }
            onConnectClicked(addr, port);
        }
    });
    m_connectButton = connectBtn.get();

    // Back button (return from multiplayer panel)
    auto backBtn = std::make_unique<UIButton>();
    backBtn->setText("BACK");
    backBtn->setTextScale(2.0f);
    backBtn->width = 300.0f;
    backBtn->height = 48.0f;
    backBtn->anchor = Anchor::Center;
    backBtn->anchorOffsetY = -140.0f;
    backBtn->setTone(UIButtonTone::Secondary);
    backBtn->visible = false;
    backBtn->setOnClick([this]() { hideMultiplayerPanel(); });
    m_backButton = backBtn.get();

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
    addRoot(std::move(modelSceneBtn));
    addRoot(std::move(mpBtn));
    addRoot(std::move(quitBtn));
    addRoot(std::move(addrInput));
    addRoot(std::move(connectBtn));
    addRoot(std::move(backBtn));
    addRoot(std::move(langDropdown));

    // Register tweens
    registerFloatTween(m_titleY);
    registerFloatTween(m_titleGlow);
}

void MainMenuScreen::refreshTexts() {
    if (!getLocaleManager())
        return;
    if (m_startButton)
        m_startButton->setText(getLocaleManager()->tr("start_game"));
    if (m_modelSceneButton)
        m_modelSceneButton->setText(getLocaleManager()->tr("model_scene"));
    if (m_multiplayerButton)
        m_multiplayerButton->setText(getLocaleManager()->tr("multiplayer"));
    if (m_quitButton)
        m_quitButton->setText(getLocaleManager()->tr("quit"));
}

void MainMenuScreen::showMultiplayerPanel() {
    // Hide main menu buttons
    if (m_startButton)
        m_startButton->visible = false;
    if (m_modelSceneButton)
        m_modelSceneButton->visible = false;
    if (m_multiplayerButton)
        m_multiplayerButton->visible = false;
    if (m_quitButton)
        m_quitButton->visible = false;
    // Show multiplayer panel
    if (m_addressInput)
        m_addressInput->visible = true;
    if (m_connectButton)
        m_connectButton->visible = true;
    if (m_backButton)
        m_backButton->visible = true;
}

void MainMenuScreen::hideMultiplayerPanel() {
    // Show main menu buttons
    if (m_startButton)
        m_startButton->visible = true;
    if (m_modelSceneButton)
        m_modelSceneButton->visible = true;
    if (m_multiplayerButton)
        m_multiplayerButton->visible = true;
    if (m_quitButton)
        m_quitButton->visible = true;
    // Hide multiplayer panel
    if (m_addressInput)
        m_addressInput->visible = false;
    if (m_connectButton)
        m_connectButton->visible = false;
    if (m_backButton)
        m_backButton->visible = false;
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
