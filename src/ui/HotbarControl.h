#pragma once

#include <array>
#include <glad/glad.h>

#include "IUIControl.h"

class Window;
class Inventory;
class ResourceMgr;
class Shader;

class HotbarControl : public IUIControl
{
public:
    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;

    void render(const UIRenderContext& context) const override;
    UIEventResult onInput(const UIInputEvent& event) override;
    [[nodiscard]] bool isVisible() const override;

    // Backward-compatible API.
    void render(const Window& window, const Inventory& inventory) const;
    void setInventorySource(const Inventory* inventory);
    void setVisible(bool visible);

    void setBgColor(const std::array<float, 4>& color);
    [[nodiscard]] const std::array<float, 4>& getBgColor() const;

    void setBorderColor(const std::array<float, 4>& color);
    [[nodiscard]] const std::array<float, 4>& getBorderColor() const;

    void setIconTintColor(const std::array<float, 4>& color);
    [[nodiscard]] const std::array<float, 4>& getIconTintColor() const;

private:
    void initMesh();
    void cleanupMesh();
    void renderInternal(float screenW, float screenH, const Inventory& inventory) const;

    Shader* m_inventoryShader = nullptr;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    ResourceMgr* m_resourceMgr = nullptr;
    const Inventory* m_inventory = nullptr;
    bool m_visible = true;

    std::array<float, 4> m_bgColor {1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 4> m_borderColor {1.0f, 1.0f, 1.0f, 0.9f};
    std::array<float, 4> m_iconTintColor {1.0f, 1.0f, 1.0f, 1.0f};
};

