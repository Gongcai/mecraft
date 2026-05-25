#pragma once

#include <array>
#include <string>
#include <vector>
#include <glad/glad.h>

#include "GlyphAtlas.h"

class ResourceMgr;
class Shader;

class TextRenderer
{
public:
    struct TextMetrics {
        float width = 0.0f;
        float height = 0.0f;
    };

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

    // Text measurement using per-glyph metrics from FreeType.
    [[nodiscard]] TextMetrics measureText(const std::string& text, float scale) const;

    [[nodiscard]] const GlyphAtlas& getAtlas() const { return m_atlas; }

private:
    void initMesh();
    void cleanupMesh();
    void generateQuads(const std::string& text,
                       float x,
                       float y,
                       float scale,
                       const std::array<float, 4>& color,
                       std::vector<float>& outVertices) const;
    static void appendVertex(std::vector<float>& outVertices,
                             float x,
                             float y,
                             float u,
                             float v,
                             const std::array<float, 4>& color);

    Shader* m_textShader = nullptr;
    GlyphAtlas m_atlas;

    GLuint m_textVao = 0;
    GLuint m_textVbo = 0;

    // Batch state (mutable for const batch methods).
    mutable bool m_batchActive = false;
    mutable float m_batchScreenWidth = 0.0f;
    mutable float m_batchScreenHeight = 0.0f;
    mutable std::vector<float> m_batchVertices;
};
