#include "TextRenderer.h"

#include <algorithm>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include "../Paths.h"
#include "../renderer/Shader.h"
#include "../resource/ResourceMgr.h"

static uint32_t decodeUtf8(const char*& ptr, const char* end) {
    auto c = static_cast<unsigned char>(*ptr);
    if (c < 0x80) { ptr += 1; return c; }
    if ((c & 0xE0) == 0xC0) {
        if (end - ptr < 2) { ptr += 1; return 0xFFFD; }
        ptr += 2;
        return static_cast<uint32_t>((c & 0x1F) << 6) |
               (static_cast<unsigned char>(ptr[-1]) & 0x3F);
    }
    if ((c & 0xF0) == 0xE0) {
        if (end - ptr < 3) { ptr += 1; return 0xFFFD; }
        ptr += 3;
        return static_cast<uint32_t>((c & 0x0F) << 12) |
               (static_cast<uint32_t>(static_cast<unsigned char>(ptr[-2]) & 0x3F) << 6) |
               (static_cast<unsigned char>(ptr[-1]) & 0x3F);
    }
    if ((c & 0xF8) == 0xF0) {
        if (end - ptr < 4) { ptr += 1; return 0xFFFD; }
        ptr += 4;
        return static_cast<uint32_t>((c & 0x07) << 18) |
               (static_cast<uint32_t>(static_cast<unsigned char>(ptr[-3]) & 0x3F) << 12) |
               (static_cast<uint32_t>(static_cast<unsigned char>(ptr[-2]) & 0x3F) << 6) |
               (static_cast<unsigned char>(ptr[-1]) & 0x3F);
    }
    ptr += 1;
    return 0xFFFD;
}

void TextRenderer::init(ResourceMgr& resourceMgr)
{
    m_textShader = resourceMgr.getShader("text");
    m_atlas.init(DEFAULT_FONT_PATH, 32);
    initMesh();
}

void TextRenderer::shutdown()
{
    cleanupMesh();
    m_textShader = nullptr;
    m_atlas.shutdown();
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

TextRenderer::TextMetrics TextRenderer::measureText(const std::string& text, float scale) const
{
    TextMetrics result{};
    if (text.empty()) return result;

    const float renderScale = std::max(0.1f, scale);
    const float pixelScale = (8.0f * renderScale) / static_cast<float>(m_atlas.getPixelHeight());
    const float lineHeight = static_cast<float>(m_atlas.getLineHeight()) * pixelScale;
    float maxWidth = 0.0f;
    float currentWidth = 0.0f;
    int lineCount = 1;

    const char* ptr = text.data();
    const char* end = ptr + text.size();
    while (ptr < end) {
        if (*ptr == '\n') {
            maxWidth = std::max(maxWidth, currentWidth);
            currentWidth = 0.0f;
            lineCount++;
            ptr++;
            continue;
        }
        uint32_t code = decodeUtf8(ptr, end);
        const GlyphInfo& g = m_atlas.getGlyph(code);
        currentWidth += static_cast<float>(g.advanceX >> 6) * pixelScale;
    }
    maxWidth = std::max(maxWidth, currentWidth);

    result.width = maxWidth;
    result.height = lineHeight * static_cast<float>(lineCount);
    return result;
}

void TextRenderer::generateQuads(const std::string& text,
                                 float x,
                                 float y,
                                 float scale,
                                 const std::array<float, 4>& color,
                                 std::vector<float>& outVertices) const
{
    (void)color;

    const float renderScale = std::max(0.1f, scale);
    const float pixelScale = (8.0f * renderScale) / static_cast<float>(m_atlas.getPixelHeight());
    const float lineHeight = static_cast<float>(m_atlas.getLineHeight()) * pixelScale;

    // cursorY = baseline position. Place baseline at y + descent so that the
    // text bottom (baseline - descent) aligns with the caller's y coordinate.
    const float descent = static_cast<float>(m_atlas.getDescent()) * pixelScale;

    const float originX = x;
    float cursorX = x;
    float cursorY = y + descent;

    const char* ptr = text.data();
    const char* end = ptr + text.size();
    while (ptr < end) {
        if (*ptr == '\n') {
            cursorX = originX;
            cursorY -= lineHeight;
            ptr++;
            continue;
        }

        uint32_t code = decodeUtf8(ptr, end);

        const GlyphInfo& g = m_atlas.getGlyph(code);

        const float xPos = cursorX + static_cast<float>(g.bearingX) * pixelScale;
        const float yPos = cursorY - static_cast<float>(g.bitmapHeight - g.bearingY) * pixelScale;
        const float w = static_cast<float>(g.bitmapWidth) * pixelScale;
        const float h = static_cast<float>(g.bitmapHeight) * pixelScale;

        outVertices.push_back(xPos);     outVertices.push_back(yPos);     outVertices.push_back(g.uvMinX); outVertices.push_back(g.uvMinY);
        outVertices.push_back(xPos + w); outVertices.push_back(yPos);     outVertices.push_back(g.uvMaxX); outVertices.push_back(g.uvMinY);
        outVertices.push_back(xPos + w); outVertices.push_back(yPos + h); outVertices.push_back(g.uvMaxX); outVertices.push_back(g.uvMaxY);
        outVertices.push_back(xPos);     outVertices.push_back(yPos);     outVertices.push_back(g.uvMinX); outVertices.push_back(g.uvMinY);
        outVertices.push_back(xPos + w); outVertices.push_back(yPos + h); outVertices.push_back(g.uvMaxX); outVertices.push_back(g.uvMaxY);
        outVertices.push_back(xPos);     outVertices.push_back(yPos + h); outVertices.push_back(g.uvMinX); outVertices.push_back(g.uvMaxY);

        cursorX += static_cast<float>(g.advanceX >> 6) * pixelScale;
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
    if (!m_textShader || m_atlas.getTexture() == 0 || m_textVao == 0 || m_textVbo == 0 || text.empty()) {
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
    glBindTexture(GL_TEXTURE_2D, m_atlas.getTexture());

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

    if (m_batchVertices.empty() || !m_textShader || m_atlas.getTexture() == 0 ||
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
    glBindTexture(GL_TEXTURE_2D, m_atlas.getTexture());

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
