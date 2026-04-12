#pragma once

#include <array>
#include <glad/glad.h>

class Window;
class Inventory;
class ResourceMgr;
class Shader;

class HotbarControl
{
public:
    void init(ResourceMgr& resourceMgr);
    void shutdown();

    void render(const Window& window, const Inventory& inventory) const;

    void setBgColor(const std::array<float, 4>& color);
    [[nodiscard]] const std::array<float, 4>& getBgColor() const;

    void setBorderColor(const std::array<float, 4>& color);
    [[nodiscard]] const std::array<float, 4>& getBorderColor() const;

    void setIconTintColor(const std::array<float, 4>& color);
    [[nodiscard]] const std::array<float, 4>& getIconTintColor() const;

private:
    void initMesh();
    void cleanupMesh();

    Shader* m_inventoryShader = nullptr;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    ResourceMgr* m_resourceMgr = nullptr;

    std::array<float, 4> m_bgColor {1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 4> m_borderColor {1.0f, 1.0f, 1.0f, 0.9f};
    std::array<float, 4> m_iconTintColor {1.0f, 1.0f, 1.0f, 1.0f};
};

