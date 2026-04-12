#pragma once

#include <array>
#include <string>
#include <glad/glad.h>

class ResourceMgr;
class Shader;

class TextRenderer
{
public:
    void init(ResourceMgr& resourceMgr);
    void shutdown();

    void render(const std::string& text,
                float x,
                float y,
                float scale,
                const std::array<float, 4>& color,
                float screenWidth,
                float screenHeight) const;

    void setAdvanceFactor(float factor);
    [[nodiscard]] float getAdvanceFactor() const;

private:
    void initMesh();
    void cleanupMesh();

    Shader* m_textShader = nullptr;
    GLuint m_textVao = 0;
    GLuint m_textVbo = 0;
    GLuint m_fontTexture = 0;
    float m_textAdvanceFactor = 0.70f;
};

