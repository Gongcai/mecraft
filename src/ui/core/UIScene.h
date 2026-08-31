#pragma once

#include <memory>
#include <vector>

#include "UIWidget.h"
#include "TweenGroup.h"

class LocaleManager;

class UIScene : public UIWidget {
public:
    enum class Phase {
        Entering,
        Active,
        Exiting,
    };

    UIScene() { visible = true; }
    ~UIScene() override = default;

    void init(GameResources& resources, RhiDevice& rhiDevice) override;
    void shutdown() override;
    void layout(const UIRenderContext& context) override;
    void render(const UIRenderContext& context) const override;
    UIEventResult onInput(const UIInputEvent& event, const UIRenderContext& ctx) override;
    void setInputContext(const UIRenderContext& context);
    void moveFocusNext(const std::vector<UIWidget*>* cached = nullptr);
    void moveFocusPrev(const std::vector<UIWidget*>* cached = nullptr);

    // Scene lifecycle
    void enterScene();
    void exitScene();
    [[nodiscard]] bool isTransitioning() const { return m_phase != Phase::Active; }
    [[nodiscard]] Phase getPhase() const { return m_phase; }

    // Animation management
    void updateAnimations(float dt) override;
    void registerFloatTween(Tween<float>& tween) { m_animations.addFloat(tween); }
    void registerColorTween(Tween<std::array<float, 4>>& tween) { m_animations.addColor(tween); }

    // Widget management
    void addRoot(std::unique_ptr<UIWidget> widget) { m_roots.push_back(std::move(widget)); }

    // Locale
    void setLocaleManager(const LocaleManager* locale) { m_locale = locale; }

protected:
    // Subclasses override these to set up enter/exit animations
    virtual void onSceneEnter() {}
    virtual void onSceneExit() {}

    // Called during init to create widgets
    virtual void buildUI(GameResources& resources, RhiDevice& rhiDevice) {
        (void)resources;
        (void)rhiDevice;
    }

    [[nodiscard]] GameResources* getResources() const { return m_resources; }
    [[nodiscard]] RhiDevice* getRhiDevice() const { return m_rhiDevice; }
    [[nodiscard]] const LocaleManager* getLocaleManager() const { return m_locale; }

private:
    void ensureFocusableSelection(const std::vector<UIWidget*>* cached = nullptr);
    void collectAllFocusable(std::vector<UIWidget*>& out) const;
    void applyPendingFocusRequests();
    void setFocusedWidget(UIWidget* widget);

    std::vector<std::unique_ptr<UIWidget>> m_roots;
    TweenGroup m_animations;
    Phase m_phase = Phase::Active;
    GameResources* m_resources = nullptr;
    RhiDevice* m_rhiDevice = nullptr;
    const LocaleManager* m_locale = nullptr;
    mutable UIRenderContext m_currentContext;
    bool m_initialized = false;
    bool m_hasInputContext = false;
    UIWidget* m_focusedWidget = nullptr;
    bool m_focusEngaged = false;
    float m_lastPointerX = -1.0f;
    float m_lastPointerY = -1.0f;
};
