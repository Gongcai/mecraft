#pragma once

#include <array>
#include <string>
#include <vector>
#include <glad/glad.h>

class ResourceMgr;
class Shader;

class TextRenderer
{
public:
    void init(ResourceMgr& resourceMgr);
    void shutdown();

    // Single-shot render (backward compatible).
    void render(const std::string& text,
                float x,
                float y,
                float scale,
                const std::array<float, 4>& color,
                float screenWidth,
                float screenHeight) const;

    // Batch API: accumulate multiple text draws into one GL draw call.
    void beginBatch(float screenWidth, float screenHeight) const;
    void batchRender(const std::string& text,
                     float x,
                     float y,
                     float scale,
                     const std::array<float, 4>& color) const;
    void endBatch() const;

    void setAdvanceFactor(float factor);
    [[nodiscard]] float getAdvanceFactor() const;

private:
    void initMesh();
    void cleanupMesh();
    void generateQuads(const std::string& text,
                       float x,
                       float y,
                       float scale,
                       const std::array<float, 4>& color,
                       std::vector<float>& outVertices) const;

    Shader* m_textShader = nullptr;
    GLuint m_textVao = 0;
    GLuint m_textVbo = 0;
    GLuint m_fontTexture = 0;
    float m_textAdvanceFactor = 0.70f;

    // Batch state (mutable for const batch methods).
    mutable bool m_batchActive = false;
    mutable float m_batchScreenWidth = 0.0f;
    mutable float m_batchScreenHeight = 0.0f;
    mutable std::vector<float> m_batchVertices;
};

