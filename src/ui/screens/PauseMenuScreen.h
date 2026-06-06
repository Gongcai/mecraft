#pragma once

#include <functional>

#include "../core/UIScene.h"
#include "../core/Tween.h"

class UIPanel;
class UIText;
class UIButton;
class UIStackLayout;

class PauseMenuScreen : public UIScene {
public:
    std::function<void()> onResume;
    std::function<void()> onSettings;
    std::function<void()> onQuitToMenu;

    void layout(const UIRenderContext& ctx) override;
    void updateAnimations(float dt) override;

protected:
    void buildUI(ResourceMgr& resourceMgr) override;
    void onSceneEnter() override;
    void onSceneExit() override;

private:
    UIPanel* m_overlay = nullptr;
    UIText* m_title = nullptr;
    UIButton* m_resumeButton = nullptr;
    UIButton* m_settingsButton = nullptr;
    UIButton* m_quitButton = nullptr;
    UIStackLayout* m_buttonStack = nullptr;

    // Overlay fade-in
    Tween<float> m_overlayAlpha;

    // Button slide-in
    Tween<float> m_buttonSlideX;

    static constexpr float kOverlayFadeDuration = 0.3f;
    static constexpr float kButtonSlideDuration = 0.4f;
};
