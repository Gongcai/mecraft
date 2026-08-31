#pragma once

#include <array>
#include <functional>
#include <vector>
#include <string>

#include "../core/UIScene.h"
#include "../core/Tween.h"

class UIImage;
class UIButton;
class UIDropdown;
class UITextInput;

class MainMenuScreen : public UIScene {
public:
    std::function<void()> onStartClicked;
    std::function<void()> onModelSceneClicked;
    std::function<void()> onMultiplayerClicked;
    std::function<void(const std::string& address, int port)> onConnectClicked;
    std::function<void()> onQuitClicked;

    void updateAnimations(float dt) override;

protected:
    void buildUI(GameResources& resources, RhiDevice& rhiDevice) override;
    void onSceneEnter() override;

private:
    void refreshTexts();
    void showMultiplayerPanel();
    void hideMultiplayerPanel();

    UIImage* m_title = nullptr;
    UIButton* m_startButton = nullptr;
    UIButton* m_modelSceneButton = nullptr;
    UIButton* m_multiplayerButton = nullptr;
    UIButton* m_quitButton = nullptr;
    UIButton* m_connectButton = nullptr;
    UIButton* m_backButton = nullptr;
    UITextInput* m_addressInput = nullptr;
    UIDropdown* m_langDropdown = nullptr;

    std::vector<std::string> m_langCodes;

    // Title animation tweens
    Tween<float> m_titleY;
    Tween<float> m_titleGlow;

    static constexpr float kTitleDropDuration = 1.2f;
    static constexpr float kGlowPeriod = 2.0f;
};
