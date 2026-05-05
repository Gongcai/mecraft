#pragma once

#include <memory>
#include <vector>

#include "IUIControl.h"
#include "UIWidget.h"
#include "TweenGroup.h"

class UIScene : public IUIControl {
public:
    enum class Phase {
        Entering,
        Active,
        Exiting,
    };

    UIScene() = default;
    ~UIScene() override = default;

    // IUIControl interface
    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;
    void render(const UIRenderContext& context) const override;
    UIEventResult onInput(const UIInputEvent& event) override;
    [[nodiscard]] bool isVisible() const override { return true; }
    void setInputContext(const UIRenderContext& context);
    void moveFocusNext();
    void moveFocusPrev();

    // Scene lifecycle
    void enterScene();
    void exitScene();
    [[nodiscard]] bool isTransitioning() const { return m_phase != Phase::Active; }
    [[nodiscard]] Phase getPhase() const { return m_phase; }

    // Animation management
    virtual void updateAnimations(float dt);
    void registerFloatTween(Tween<float>& tween) { m_animations.addFloat(tween); }
    void registerColorTween(Tween<std::array<float, 4>>& tween) { m_animations.addColor(tween); }

    // Widget management
    void addRoot(std::unique_ptr<UIWidget> widget) { m_roots.push_back(std::move(widget)); }

protected:
    // Subclasses override these to set up enter/exit animations
    virtual void onSceneEnter() {}
    virtual void onSceneExit() {}

    // Called during init to create widgets
    virtual void buildUI(ResourceMgr& resourceMgr) { (void)resourceMgr; }

    [[nodiscard]] ResourceMgr* getResourceMgr() const { return m_resourceMgr; }

private:
    void ensureFocusableSelection();
    void applyPendingFocusRequests();
    void setFocusedWidget(UIWidget* widget);

    std::vector<std::unique_ptr<UIWidget>> m_roots;
    TweenGroup m_animations;
    Phase m_phase = Phase::Active;
    ResourceMgr* m_resourceMgr = nullptr;
    mutable UIRenderContext m_currentContext;
    bool m_hasInputContext = false;
    UIWidget* m_focusedWidget = nullptr;
};
