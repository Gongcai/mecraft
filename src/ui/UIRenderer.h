#pragma once

#include <glad/glad.h>
#include <vector>

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

private:
    void initCrosshairMesh();
    void renderCrosshair(const Window& window);
    void cleanupCrosshairMesh();

    Shader* m_crosshairShader = nullptr;
    GLuint m_crosshairVao = 0;
    GLuint m_crosshairVbo = 0;
    int m_vertexCount = 0;
};
