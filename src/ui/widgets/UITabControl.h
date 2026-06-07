#pragma once

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <glad/glad.h>

#include "../core/UIStyle.h"
#include "../core/UIWidget.h"

class Shader;

// Tab control widget with clickable headers and switchable content panels.
// Content widgets are owned by the tab control and shown/hidden based on the active tab.
class UITabControl : public UIWidget {
public:
    UITabControl();
    ~UITabControl() override;

    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;

    // Add a tab with the given title. Returns the tab index.
    // Use getContentPanel(index) to add child widgets to the tab's content area.
    int addTab(const std::string& title);

    // Get the content panel widget for a tab. Add child widgets to this panel.
    [[nodiscard]] UIWidget* getContentPanel(int index);

    void setActiveTab(int index);
    [[nodiscard]] int getActiveTab() const { return m_activeIndex; }
    [[nodiscard]] int getTabCount() const { return static_cast<int>(m_tabs.size()); }
    [[nodiscard]] float getHeaderHeight(const UIRenderContext& ctx) const;

    // Local color overrides.
    void setHeaderColor(const Color& styleColor);
    void setHeaderActiveColor(const Color& styleColor);
    void setHeaderHoverColor(const Color& styleColor);
    void setIndicatorColor(const Color& styleColor);
    void setContentColor(const Color& styleColor);
    void setStyle(const UITabControlStyle& style);
    void clearLocalStyle();

    std::function<void(int)> onTabChanged;

protected:
    void renderSelf(const UIRenderContext& ctx) const override;
    UIEventResult onInput(const UIInputEvent& event, const UIRenderContext& ctx) override;
    void render(const UIRenderContext& ctx) const override;

private:
    struct Tab {
        std::string title;
        UIWidget* contentPanel = nullptr;
    };

    [[nodiscard]] int hitTestHeader(float px, float py, const UIRenderContext& ctx) const;
    [[nodiscard]] UITabControlStyle resolveBaseStyle(const UIRenderContext& ctx) const;
    [[nodiscard]] UIResolvedTabControlStyle resolveStyle(const UIRenderContext& ctx, int state) const;

    Shader* m_shader = nullptr;
    Shader* m_glassShader = nullptr;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;

    std::vector<Tab> m_tabs;
    int m_activeIndex = 0;
    int m_hoveredTab = -1;

    bool m_hasLocalColors = false;
    bool m_hasLocalStyle = false;
    Color m_headerColor{0.20f, 0.20f, 0.20f, 0.9f};
    Color m_headerActiveColor{0.28f, 0.28f, 0.28f, 1.0f};
    Color m_headerHoverColor{0.25f, 0.25f, 0.25f, 1.0f};
    Color m_indicatorColor{0.2f, 0.8f, 1.0f, 1.0f};
    Color m_contentColor{0.18f, 0.18f, 0.18f, 0.85f};
    UITabControlStyle m_localStyle;
};
