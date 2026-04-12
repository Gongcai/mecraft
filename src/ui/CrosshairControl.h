#pragma once

#include <array>

#include <glad/glad.h>

class Window;
class ResourceMgr;
class Shader;

class CrosshairControl
{
public:
    void init(ResourceMgr& resourceMgr);
    void shutdown();

    void render(const Window& window) const;

    void setSize(float size);
    [[nodiscard]] float getSize() const;

    void setColor(const std::array<float, 4>& color);
    [[nodiscard]] const std::array<float, 4>& getColor() const;

private:
    void initMesh();
    void rebuildMesh();
    void cleanupMesh();

    Shader* m_shader = nullptr;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    int m_vertexCount = 0;
    float m_size = 1.0f;
    std::array<float, 4> m_color {1.0f, 1.0f, 1.0f, 1.0f};
};

