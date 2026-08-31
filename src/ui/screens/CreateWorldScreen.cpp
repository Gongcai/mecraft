#include "CreateWorldScreen.h"

#include "../widgets/UIButton.h"
#include "../widgets/UIPanel.h"
#include "../widgets/UIText.h"
#include "../widgets/UITextInput.h"
#include "../../resource/GameResources.h"
#include "../../locale/LocaleManager.h"

#include <charconv>
#include <filesystem>
#include <functional>
#include <string>

// ---------------------------------------------------------------------------
// buildUI
// ---------------------------------------------------------------------------

void CreateWorldScreen::buildUI(GameResources& resources, RhiDevice& rhiDevice) {
    (void)resources;

    // -- Background overlay --
    auto bgPanel = std::make_unique<UIPanel>();
    bgPanel->setTone(UIPanelTone::Overlay);
    bgPanel->anchor = Anchor::BottomLeft;
    bgPanel->width = 9999.0f;
    bgPanel->height = 9999.0f;
    m_bgPanel = bgPanel.get();
    addRoot(std::move(bgPanel));

    // -- Title --
    auto title = std::make_unique<UIText>();
    title->setText(getLocaleManager() ? getLocaleManager()->tr("create_new_world") : "CREATE NEW WORLD");
    title->setTextScale(3.0f);
    title->setTone(UITextTone::OnOverlay);
    title->setAlignment(TextAlignment::Center);
    title->anchor = Anchor::TopCenter;
    title->anchorOffsetY = -60.0f;
    title->width = 500.0f;
    title->height = 40.0f;
    m_title = title.get();
    addRoot(std::move(title));

    // -- World name label --
    auto nameLabel = std::make_unique<UIText>();
    nameLabel->setText(getLocaleManager() ? getLocaleManager()->tr("world_name") : "World name:");
    nameLabel->setTextScale(1.4f);
    nameLabel->setTone(UITextTone::OnOverlaySecondary);
    nameLabel->setAlignment(TextAlignment::Center);
    nameLabel->anchor = Anchor::Center;
    nameLabel->anchorOffsetY = 110.0f;
    nameLabel->width = 400.0f;
    nameLabel->height = 24.0f;
    m_nameLabel = nameLabel.get();
    addRoot(std::move(nameLabel));

    // -- World name input --
    auto nameInput = std::make_unique<UITextInput>();
    nameInput->width = 350.0f;
    nameInput->height = 36.0f;
    nameInput->anchor = Anchor::Center;
    nameInput->anchorOffsetY = 72.0f;
    nameInput->setPlaceholder(getLocaleManager() ? getLocaleManager()->tr("world_name_placeholder") : "New World");
    nameInput->setMaxLength(96);
    m_nameInput = nameInput.get();
    addRoot(std::move(nameInput));

    // -- Seed label --
    auto seedLabel = std::make_unique<UIText>();
    seedLabel->setText(getLocaleManager() ? getLocaleManager()->tr("enter_seed")
                                          : "Enter seed (leave empty for random):");
    seedLabel->setTextScale(1.4f);
    seedLabel->setTone(UITextTone::OnOverlaySecondary);
    seedLabel->setAlignment(TextAlignment::Center);
    seedLabel->anchor = Anchor::Center;
    seedLabel->anchorOffsetY = 26.0f;
    seedLabel->width = 400.0f;
    seedLabel->height = 24.0f;
    m_seedLabel = seedLabel.get();
    addRoot(std::move(seedLabel));

    // -- Seed input --
    auto seedInput = std::make_unique<UITextInput>();
    seedInput->width = 350.0f;
    seedInput->height = 36.0f;
    seedInput->anchor = Anchor::Center;
    seedInput->anchorOffsetY = -12.0f;
    seedInput->setPlaceholder(getLocaleManager() ? getLocaleManager()->tr("seed_placeholder") : "Enter seed...");
    seedInput->setMaxLength(64);
    m_seedInput = seedInput.get();
    addRoot(std::move(seedInput));

    // -- "Start New Game" button --
    auto startBtn = std::make_unique<UIButton>();
    startBtn->setText(getLocaleManager() ? getLocaleManager()->tr("start_new_game") : "START NEW GAME");
    startBtn->setTextScale(2.0f);
    startBtn->width = 300.0f;
    startBtn->height = 50.0f;
    startBtn->anchor = Anchor::Center;
    startBtn->anchorOffsetY = -82.0f;
    startBtn->setTone(UIButtonTone::Success);
    startBtn->setOnClick([this]() {
        int seed = parseSeed(m_seedInput ? m_seedInput->getText() : "");
        const std::string displayName = trimDisplayName(m_nameInput ? m_nameInput->getText() : "");
        if (onCreateWorld)
            onCreateWorld(seed, displayName);
    });
    m_startButton = startBtn.get();
    addRoot(std::move(startBtn));

    // -- "Back" button --
    auto backBtn = std::make_unique<UIButton>();
    backBtn->setText(getLocaleManager() ? getLocaleManager()->tr("back") : "BACK");
    backBtn->setTextScale(1.8f);
    backBtn->width = 180.0f;
    backBtn->height = 44.0f;
    backBtn->anchor = Anchor::Center;
    backBtn->anchorOffsetY = -152.0f;
    backBtn->setTone(UIButtonTone::Secondary);
    backBtn->setOnClick([this]() {
        if (onBackClicked)
            onBackClicked();
    });
    m_backButton = backBtn.get();
    addRoot(std::move(backBtn));

    registerFloatTween(m_titleSlideY);
}

// ---------------------------------------------------------------------------
// Scene lifecycle
// ---------------------------------------------------------------------------

void CreateWorldScreen::onSceneEnter() {
    // Reset world name input
    if (m_nameInput) {
        m_nameInput->setText("");
    }

    // Reset seed input
    if (m_seedInput) {
        m_seedInput->setText("");
    }

    // Title drops in
    m_titleSlideY.start(-80.0f, -60.0f, kTitleDropDuration, EasingType::BackOut);
}

void CreateWorldScreen::updateAnimations(float dt) {
    UIScene::updateAnimations(dt);
    if (m_title) {
        m_title->anchorOffsetY = m_titleSlideY.value();
    }
}

// ---------------------------------------------------------------------------
// Seed parsing: numeric text maps directly; other text maps through std::hash.
// ---------------------------------------------------------------------------

int CreateWorldScreen::parseSeed(const std::string& text) {
    if (text.empty())
        return 1234;

    long long value = 0;
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec == std::errc{} && result.ptr == end) {
        return static_cast<int>(value);
    }

    return static_cast<int>(std::hash<std::string>{}(text));
}

std::string CreateWorldScreen::trimDisplayName(const std::string& text) {
    const auto start = text.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) {
        return "";
    }
    const auto end = text.find_last_not_of(" \t\n\r");
    return text.substr(start, end - start + 1);
}

// ---------------------------------------------------------------------------
// Auto-generate a unique world name
// ---------------------------------------------------------------------------

std::string CreateWorldScreen::generateWorldName(const std::filesystem::path& saveRoot) {
    std::error_code ec;
    if (!std::filesystem::exists(saveRoot, ec) || !std::filesystem::is_directory(saveRoot, ec)) {
        return "New World";
    }

    // Check if "New World" is available
    const std::string base = "New World";
    if (!std::filesystem::exists(saveRoot / base, ec)) {
        return base;
    }

    // Find the next available "New World (N)"
    for (int i = 2; i < 10000; ++i) {
        std::string candidate = base + " (" + std::to_string(i) + ")";
        if (!std::filesystem::exists(saveRoot / candidate, ec)) {
            return candidate;
        }
    }

    return base + " (" + std::to_string(10000) + ")";
}
