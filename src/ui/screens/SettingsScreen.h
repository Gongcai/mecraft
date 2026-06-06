#pragma once

#include <functional>

#include "../core/UIScene.h"
#include "../core/Tween.h"

class UIPanel;
class UIText;
class UIButton;
class UITabControl;
class RenderSettings;
class RenderScene;
class World;

/// In-game settings screen accessible from the pause menu.
/// Provides sliders, toggles, and dropdowns for render settings.
class SettingsScreen : public UIScene {
public:
    std::function<void()> onBack;

    void setRenderScene(RenderScene* rs) { m_renderScene = rs; }
    void setWorld(World* world) { m_world = world; }
    void setRenderDistanceSetter(std::function<void(int)> setter) { m_renderDistanceSetter = std::move(setter); }

    void updateAnimations(float dt) override;

protected:
    void buildUI(ResourceMgr& resourceMgr) override;
    void onSceneEnter() override;
    void onSceneExit() override;

private:
    // Tab builders
    void buildGeneralTab(UIWidget* contentPanel, ResourceMgr& resourceMgr);
    void buildShadowsTab(UIWidget* contentPanel, ResourceMgr& resourceMgr);
    void buildLightingTab(UIWidget* contentPanel, ResourceMgr& resourceMgr);
    void buildPostProcessTab(UIWidget* contentPanel, ResourceMgr& resourceMgr);
    void buildVolumetricTab(UIWidget* contentPanel, ResourceMgr& resourceMgr);
    void buildUpscaleTab(UIWidget* contentPanel, ResourceMgr& resourceMgr);

    // Helper to create a labeled slider row
    void addSliderRow(UIWidget* parent, ResourceMgr& resourceMgr,
                      const std::string& label, float minVal, float maxVal,
                      float currentVal, float step, std::function<void(float)> onValueChanged);

    // Helper to create a labeled dropdown row
    void addDropdownRow(UIWidget* parent, ResourceMgr& resourceMgr,
                        const std::string& label, const std::vector<std::string>& options,
                        int currentIndex, std::function<void(int, const std::string&)> onSelectionChanged);

    // Helper to create a toggle
    void addToggle(UIWidget* parent, ResourceMgr& resourceMgr,
                   const std::string& label, bool checked, std::function<void(bool)> onChanged);

    // Helper to create a section header
    void addSectionHeader(UIWidget* parent, ResourceMgr& resourceMgr, const std::string& text);

    RenderScene* m_renderScene = nullptr;
    World* m_world = nullptr;
    std::function<void(int)> m_renderDistanceSetter;

    UIPanel* m_overlay = nullptr;
    UIText* m_title = nullptr;
    UIButton* m_backButton = nullptr;
    UITabControl* m_tabControl = nullptr;

    // Overlay fade-in
    Tween<float> m_overlayAlpha;

    // Content slide-in
    Tween<float> m_contentSlideX;

    static constexpr float kOverlayFadeDuration = 0.3f;
    static constexpr float kContentSlideDuration = 0.4f;
};
