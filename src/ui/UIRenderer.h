#pragma once

#include <glad/glad.h>
#include <array>

class Window;
class ResourceMgr;
class Shader;

class UIRenderer
{
public:
    UIRenderer();
    ~UIRenderer();

    void init(ResourceMgr& resourceMgr);
    void shutdown();

    void render(const Window& window);

    void setCrosshairSize(float size);
    [[nodiscard]] float getCrosshairSize() const;

    void setCrosshairColor(const std::array<float, 4>& color);
    [[nodiscard]] const std::array<float, 4>& getCrosshairColor() const;

private:
    void initCrosshairMesh();
    void rebuildCrosshairMesh();
    void renderCrosshair(const Window& window);
    void cleanupCrosshairMesh();

    Shader* m_crosshairShader = nullptr;
    GLuint m_crosshairVao = 0;
    GLuint m_crosshairVbo = 0;
    int m_vertexCount = 0;
    float m_crosshairSize = 1.0f;
    std::array<float, 4> m_crosshairColor {1.0f, 1.0f, 1.0f, 1.0f};
};
