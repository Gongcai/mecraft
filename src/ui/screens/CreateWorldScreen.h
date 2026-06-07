#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <filesystem>

#include "../core/UIScene.h"
#include "../core/Tween.h"

class UIPanel;
class UIText;
class UIButton;
class UITextInput;

/// Screen for creating a new world. Contains name/seed inputs and a start button.
class CreateWorldScreen : public UIScene {
public:
    /// Fired when the user clicks "Start New Game".
    /// displayName is the user-visible level.json name; empty means use the default.
    std::function<void(int seed, const std::string& displayName)> onCreateWorld;

    /// Fired when the user clicks "Back".
    std::function<void()> onBackClicked;

    void updateAnimations(float dt) override;

    /// Generate a unique world name based on existing saves in saveRoot.
    /// Returns "New World", "New World (2)", "New World (3)", etc.
    static std::string generateWorldName(const std::filesystem::path& saveRoot);

protected:
    void buildUI(ResourceMgr& resourceMgr) override;
    void onSceneEnter() override;

private:
    static int parseSeed(const std::string& text);
    static std::string trimDisplayName(const std::string& text);

    UIPanel* m_bgPanel = nullptr;
    UIText* m_title = nullptr;
    UIText* m_nameLabel = nullptr;
    UITextInput* m_nameInput = nullptr;
    UIText* m_seedLabel = nullptr;
    UITextInput* m_seedInput = nullptr;
    UIButton* m_startButton = nullptr;
    UIButton* m_backButton = nullptr;

    Tween<float> m_titleSlideY;

    static constexpr float kTitleDropDuration = 0.5f;
};
