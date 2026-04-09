#pragma once

#include <glad/glad.h>
#include <array>

class Window;
class ResourceMgr;
class Shader;
class Inventory;

class UIRenderer
{
public:
    UIRenderer();
    ~UIRenderer();

    void init(ResourceMgr& resourceMgr);
    void shutdown();

    void render(const Window& window, const Inventory& inventory);

    void setCrosshairSize(float size);
    [[nodiscard]] float getCrosshairSize() const;

    void setCrosshairColor(const std::array<float, 4>& color);
    [[nodiscard]] const std::array<float, 4>& getCrosshairColor() const;

    // Hotbar colors
    void setHotbarBgColor(const std::array<float, 4>& color);
    [[nodiscard]] const std::array<float, 4>& getHotbarBgColor() const;

    void setHotbarBorderColor(const std::array<float, 4>& color);
    [[nodiscard]] const std::array<float, 4>& getHotbarBorderColor() const;

    void setHotbarIconTintColor(const std::array<float, 4>& color);
    [[nodiscard]] const std::array<float, 4>& getHotbarIconTintColor() const;

private:
    void initCrosshairMesh();
    void rebuildCrosshairMesh();
    void renderCrosshair(const Window& window);
    void cleanupCrosshairMesh();

    void initHotbarMesh();
    void cleanupHotbarMesh();
    void renderHotbar(const Window& window, const Inventory& inventory);

    // Crosshair
    Shader* m_crosshairShader = nullptr;
    GLuint m_crosshairVao = 0;
    GLuint m_crosshairVbo = 0;
    int m_vertexCount = 0;
    float m_crosshairSize = 1.0f;
    std::array<float, 4> m_crosshairColor {1.0f, 1.0f, 1.0f, 1.0f};

    // Hotbar
    Shader* m_inventoryShader = nullptr;
    GLuint m_hotbarVao = 0;
    GLuint m_hotbarVbo = 0;
    ResourceMgr* m_resourceMgr = nullptr;
    std::array<float, 4> m_hotbarBgColor {1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 4> m_hotbarBorderColor {1.0f, 1.0f, 1.0f, 0.9f};
    std::array<float, 4> m_hotbarIconTintColor {1.0f, 1.0f, 1.0f, 1.0f};
};
