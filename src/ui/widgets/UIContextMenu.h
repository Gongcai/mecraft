#pragma once

#include <array>
#include <functional>
#include <string>
#include <vector>

#include <glad/glad.h>

#include "../core/UIWidget.h"
#include "../core/Tween.h"

class Shader;

// Right-click context menu that appears at a specified screen position.
// Items can be regular clickable entries or separators.
class UIContextMenu : public UIWidget {
public:
    UIContextMenu();
    ~UIContextMenu() override;

    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;

    // Add a clickable menu item. Returns the item index.
    int addItem(const std::string& text, std::function<void()> onClick);

    // Add a visual separator line.
    void addSeparator();

    // Show the menu at the given screen position (in reference coordinates).
    void show(float x, float y);

    // Hide the menu.
    void hide();

    [[nodiscard]] bool isVisible() const { return m_menuVisible; }

protected:
    void renderSelf(const UIRenderContext& ctx) const override;
    UIEventResult onInput(const UIInputEvent& event, const UIRenderContext& ctx) override;
    void updateAnimations(float dt) override;
    void render(const UIRenderContext& ctx) const override;

private:
    enum class ItemType { Entry, Separator };

    struct MenuItem {
        ItemType type = ItemType::Entry;
        std::string text;
        std::function<void()> onClick;
    };

    [[nodiscard]] int hitTestItem(float px, float py, const UIRenderContext& ctx) const;
    void cleanupMesh();

    Shader* m_shader = nullptr;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;

    std::vector<MenuItem> m_items;
    bool m_menuVisible = false;
    int m_hoveredItem = -1;
    float m_menuX = 0.0f;
    float m_menuY = 0.0f;
    float m_scrollOffset = 0.0f;

    Tween<float> m_showTween;

    static constexpr float kItemHeight = 28.0f;
    static constexpr float kSeparatorHeight = 6.0f;
    static constexpr float kMenuPadding = 4.0f;
};
