#pragma once

#include <array>
#include <functional>

#include "../UIScene.h"
#include "../Tween.h"

class UIText;
class UIButton;

class MainMenuScreen : public UIScene {
public:
    std::function<void()> onStartClicked;
    std::function<void()> onQuitClicked;

    void updateAnimations(float dt) override;

protected:
    void buildUI(ResourceMgr& resourceMgr) override;
    void onSceneEnter() override;

private:
    UIText* m_title = nullptr;
    UIButton* m_startButton = nullptr;
    UIButton* m_quitButton = nullptr;

    // Title animation tweens
    Tween<float> m_titleY;
    Tween<float> m_titleGlow;

    static constexpr float kTitleDropDuration = 1.2f;
    static constexpr float kGlowPeriod = 2.0f;
};
