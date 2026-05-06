#pragma once

#include <array>
#include <functional>
#include <string>
#include <vector>

#include <glad/glad.h>

#include "UIWidget.h"
#include "Tween.h"

class Shader;

class UIDropdown : public UIWidget {
public:
    UIDropdown();
    ~UIDropdown() override;

    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;

    void setOptions(std::vector<std::string> options);
    void setSelectedIndex(int index);
    [[nodiscard]] int getSelectedIndex() const;
    [[nodiscard]] const std::string& getSelectedText() const;

    void setOnSelectionChanged(std::function<void(int, const std::string&)> callback);

    void updateAnimations(float dt) override;

protected:
    void renderSelf(const UIRenderContext& ctx) const override;
    UIEventResult onInput(const UIInputEvent& event, const UIRenderContext& ctx) override;

private:
    void renderCollapsed(const UIRenderContext& ctx) const;
    void renderExpanded(const UIRenderContext& ctx) const;
    [[nodiscard]] int hitTestOption(float px, float py, const UIRenderContext& ctx) const;
    [[nodiscard]] bool hitTestExpandedPanel(float px, float py, const UIRenderContext& ctx) const;

    void initMesh();
    void cleanupMesh();

    Shader* m_shader = nullptr;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;

    std::vector<std::string> m_options;
    int m_selectedIndex = -1;
    bool m_expanded = false;
    int m_hoveredOption = -1;
    float m_scrollOffset = 0.0f;

    std::array<float, 4> m_bgColor{0.22f, 0.22f, 0.22f, 0.95f};
    std::array<float, 4> m_borderColor{0.40f, 0.40f, 0.40f, 0.7f};
    std::array<float, 4> m_itemHoverColor{0.30f, 0.30f, 0.30f, 1.0f};
    std::array<float, 4> m_arrowColor{0.7f, 0.7f, 0.7f, 1.0f};

    float m_itemHeight = 28.0f;
    int m_maxVisibleItems = 8;

    std::function<void(int, const std::string&)> m_onSelectionChanged;
    Tween<float> m_expandTween;
    Tween<std::array<float, 4>> m_hoverColorTween;
    int m_prevHoveredOption = -1;
};
