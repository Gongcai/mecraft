#include "TextRenderer.h"

#include <algorithm>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include "BitmapFont.h"
#include "../renderer/Shader.h"
#include "../resource/ResourceMgr.h"

void TextRenderer::init(ResourceMgr& resourceMgr)
{
    m_textShader = resourceMgr.getShader("text");
    m_fontTexture = resourceMgr.getGuiTexture("font_ascii");
    initMesh();
}

void TextRenderer::shutdown()
{
    cleanupMesh();
    m_textShader = nullptr;
    m_fontTexture = 0;
}

void TextRenderer::initMesh()
{
    glGenVertexArrays(1, &m_textVao);
    glGenBuffers(1, &m_textVbo);

    glBindVertexArray(m_textVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_textVbo);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void TextRenderer::cleanupMesh()
{
    if (m_textVao != 0) {
        glDeleteVertexArrays(1, &m_textVao);
        m_textVao = 0;
    }
    if (m_textVbo != 0) {
        glDeleteBuffers(1, &m_textVbo);
        m_textVbo = 0;
    }
}

void TextRenderer::setAdvanceFactor(float factor)
{
    m_textAdvanceFactor = std::clamp(factor, 0.5f, 1.2f);
}

float TextRenderer::getAdvanceFactor() const
{
    return m_textAdvanceFactor;
}

void TextRenderer::generateQuads(const std::string& text,
                                 float x,
                                 float y,
                                 float scale,
                                 const std::array<float, 4>& color,
                                 std::vector<float>& outVertices) const
{
    (void)color; // color is applied at draw time via uniform, not per-vertex

    const float glyphSize = static_cast<float>(BitmapFont::kGlyphSizePx) * std::max(0.1f, scale);
    const float advance = glyphSize * m_textAdvanceFactor;

    const float originX = x;
    float cursorX = x;
    float cursorY = y;

    for (char ch : text) {
        if (ch == '\n') {
            cursorX = originX;
            cursorY -= glyphSize;
            continue;
        }

        auto code = static_cast<unsigned char>(ch);
        if (code < 32 || code > 126) {
            code = static_cast<unsigned char>('?');
        }

        const auto uv = BitmapFont::glyphUV(code);
        const float x0 = cursorX;
        const float y0 = cursorY;
        const float x1 = cursorX + glyphSize;
        const float y1 = cursorY + glyphSize;

        outVertices.push_back(x0); outVertices.push_back(y0); outVertices.push_back(uv.first.x); outVertices.push_back(uv.first.y);
        outVertices.push_back(x1); outVertices.push_back(y0); outVertices.push_back(uv.second.x); outVertices.push_back(uv.first.y);
        outVertices.push_back(x1); outVertices.push_back(y1); outVertices.push_back(uv.second.x); outVertices.push_back(uv.second.y);
        outVertices.push_back(x0); outVertices.push_back(y0); outVertices.push_back(uv.first.x); outVertices.push_back(uv.first.y);
        outVertices.push_back(x1); outVertices.push_back(y1); outVertices.push_back(uv.second.x); outVertices.push_back(uv.second.y);
        outVertices.push_back(x0); outVertices.push_back(y1); outVertices.push_back(uv.first.x); outVertices.push_back(uv.second.y);

        cursorX += advance;
    }
}

void TextRenderer::render(const std::string& text,
                          float x,
                          float y,
                          float scale,
                          const std::array<float, 4>& color,
                          float screenWidth,
                          float screenHeight) const
{
    if (!m_textShader || m_fontTexture == 0 || m_textVao == 0 || m_textVbo == 0 || text.empty()) {
        return;
    }

    std::vector<float> vertices;
    vertices.reserve(text.size() * 6 * 4);
    generateQuads(text, x, y, scale, color, vertices);

    if (vertices.empty()) {
        return;
    }

    const GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);
    GLboolean depthMaskWasEnabled = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMaskWasEnabled);
    GLint blendSrcRgb = GL_ONE;
    GLint blendDstRgb = GL_ZERO;
    GLint blendSrcAlpha = GL_ONE;
    GLint blendDstAlpha = GL_ZERO;
    glGetIntegerv(GL_BLEND_SRC_RGB, &blendSrcRgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &blendDstRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendSrcAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &blendDstAlpha);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_textShader->use();
    m_textShader->setVec2("uScreenSize", glm::vec2(screenWidth, screenHeight));
    m_textShader->setVec4("uTintColor", glm::vec4(color[0], color[1], color[2], color[3]));
    m_textShader->setInt("uFont", 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_fontTexture);

    glBindVertexArray(m_textVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_textVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size() / 4));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (blendWasEnabled) {
        glBlendFuncSeparate(blendSrcRgb, blendDstRgb, blendSrcAlpha, blendDstAlpha);
    } else {
        glDisable(GL_BLEND);
    }
    glDepthMask(depthMaskWasEnabled);
    if (depthWasEnabled) {
        glEnable(GL_DEPTH_TEST);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
}

void TextRenderer::beginBatch(float screenWidth, float screenHeight) const
{
    m_batchActive = true;
    m_batchScreenWidth = screenWidth;
    m_batchScreenHeight = screenHeight;
    m_batchVertices.clear();
}

void TextRenderer::batchRender(const std::string& text,
                               float x,
                               float y,
                               float scale,
                               const std::array<float, 4>& color) const
{
    if (!m_batchActive || text.empty()) {
        return;
    }
    m_batchVertices.reserve(m_batchVertices.size() + text.size() * 6 * 4);
    generateQuads(text, x, y, scale, color, m_batchVertices);
}

void TextRenderer::endBatch() const
{
    if (!m_batchActive) {
        return;
    }
    m_batchActive = false;

    if (m_batchVertices.empty() || !m_textShader || m_fontTexture == 0 ||
        m_textVao == 0 || m_textVbo == 0) {
        m_batchVertices.clear();
        return;
    }

    const GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);
    GLboolean depthMaskWasEnabled = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMaskWasEnabled);
    GLint blendSrcRgb = GL_ONE;
    GLint blendDstRgb = GL_ZERO;
    GLint blendSrcAlpha = GL_ONE;
    GLint blendDstAlpha = GL_ZERO;
    glGetIntegerv(GL_BLEND_SRC_RGB, &blendSrcRgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &blendDstRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendSrcAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &blendDstAlpha);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_textShader->use();
    m_textShader->setVec2("uScreenSize", glm::vec2(m_batchScreenWidth, m_batchScreenHeight));
    m_textShader->setVec4("uTintColor", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
    m_textShader->setInt("uFont", 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_fontTexture);

    glBindVertexArray(m_textVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_textVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_batchVertices.size() * sizeof(float)), m_batchVertices.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_batchVertices.size() / 4));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (blendWasEnabled) {
        glBlendFuncSeparate(blendSrcRgb, blendDstRgb, blendSrcAlpha, blendDstAlpha);
    } else {
        glDisable(GL_BLEND);
    }
    glDepthMask(depthMaskWasEnabled);
    if (depthWasEnabled) {
        glEnable(GL_DEPTH_TEST);
    } else {
        glDisable(GL_DEPTH_TEST);
    }

    m_batchVertices.clear();
}


