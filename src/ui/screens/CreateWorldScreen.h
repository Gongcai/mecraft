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

/// Screen for creating a new world. Contains a seed input and a start button.
class CreateWorldScreen : public UIScene {
public:
    /// Fired when the user clicks "Start New Game". Parameter is the parsed seed.
    std::function<void(int seed)> onCreateWorld;

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

    UIPanel* m_bgPanel = nullptr;
    UIText* m_title = nullptr;
    UIText* m_seedLabel = nullptr;
    UITextInput* m_seedInput = nullptr;
    UIButton* m_startButton = nullptr;
    UIButton* m_backButton = nullptr;

    Tween<float> m_titleSlideY;

    static constexpr float kTitleDropDuration = 0.5f;
};
