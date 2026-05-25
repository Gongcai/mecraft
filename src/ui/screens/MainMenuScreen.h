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

class MainMenuScreen : public UIScene {
public:
    std::function<void()> onStartClicked;
    std::function<void()> onQuitClicked;

    void updateAnimations(float dt) override;

protected:
    void buildUI(ResourceMgr& resourceMgr) override;
    void onSceneEnter() override;

private:
    void refreshTexts();

    UIImage* m_title = nullptr;
    UIButton* m_startButton = nullptr;
    UIButton* m_quitButton = nullptr;
    UIDropdown* m_langDropdown = nullptr;

    std::vector<std::string> m_langCodes;

    // Title animation tweens
    Tween<float> m_titleY;
    Tween<float> m_titleGlow;

    static constexpr float kTitleDropDuration = 1.2f;
    static constexpr float kGlowPeriod = 2.0f;
};
