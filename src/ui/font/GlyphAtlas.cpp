#include "GlyphAtlas.h"

#include <glad/glad.h>

#include <algorithm>
#include <cstring>
#include <cstddef>

#include <ft2build.h>
#include FT_FREETYPE_H

bool GlyphAtlas::init(const char* ttfPath, int pixelHeight)
{
    m_pixelHeight = pixelHeight;

    FT_Library library = nullptr;
    if (FT_Init_FreeType(&library) != 0) {
        return false;
    }
    m_library = library;

    FT_Face face = nullptr;
    if (FT_New_Face(library, ttfPath, 0, &face) != 0) {
        FT_Done_FreeType(library);
        m_library = nullptr;
        return false;
    }
    m_face = face;

    FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(pixelHeight));
    m_lineHeight = static_cast<int>((face->size->metrics.height) >> 6);
    m_descent = -static_cast<int>((face->size->metrics.descender) >> 6);

    m_atlasWidth = 512;
    m_atlasHeight = 512;
    m_pixelData.resize(static_cast<size_t>(m_atlasWidth) * static_cast<size_t>(m_atlasHeight), 0);
    m_cursorX = 0;
    m_cursorY = 0;
    m_rowHeight = 0;

    for (uint32_t c = 32; c <= 126; ++c) {
        rasterizeGlyph(c);
    }

    uploadAtlas();
    return true;
}

void GlyphAtlas::shutdown()
{
    if (m_texture != 0) {
        glDeleteTextures(1, &m_texture);
        m_texture = 0;
    }
    if (m_face) {
        FT_Done_Face(static_cast<FT_Face>(m_face));
        m_face = nullptr;
    }
    if (m_library) {
        FT_Done_FreeType(static_cast<FT_Library>(m_library));
        m_library = nullptr;
    }
    m_glyphs.clear();
    m_pixelData.clear();
}

const GlyphInfo& GlyphAtlas::getGlyph(uint32_t codepoint) const
{
    auto it = m_glyphs.find(codepoint);
    if (it != m_glyphs.end()) {
        return it->second;
    }
    rasterizeGlyph(codepoint);
    return m_glyphs[codepoint];
}

void GlyphAtlas::uploadPending() const
{
    if (m_dirty) {
        uploadAtlas();
    }
}

void GlyphAtlas::rasterizeGlyph(uint32_t codepoint) const
{
    auto* face = static_cast<FT_Face>(m_face);

    if (FT_Load_Char(face, static_cast<FT_ULong>(codepoint), FT_LOAD_RENDER) != 0) {
        GlyphInfo info{};
        m_glyphs[codepoint] = info;
        return;
    }

    const FT_GlyphSlot slot = face->glyph;
    const int bmpW = static_cast<int>(slot->bitmap.width);
    const int bmpH = static_cast<int>(slot->bitmap.rows);

    if (bmpW == 0 || bmpH == 0) {
        GlyphInfo info{};
        info.bearingX = static_cast<int>(slot->bitmap_left);
        info.bearingY = static_cast<int>(slot->bitmap_top);
        info.advanceX = static_cast<int>(slot->advance.x);
        m_glyphs[codepoint] = info;
        return;
    }

    if (m_cursorX + bmpW > m_atlasWidth) {
        m_cursorX = 0;
        m_cursorY += m_rowHeight + 1;
        m_rowHeight = 0;
    }
    if (m_cursorY + bmpH > m_atlasHeight) {
        int newW = m_atlasWidth;
        int newH = m_atlasHeight;
        while (m_cursorX + bmpW > newW) {
            newW *= 2;
        }
        while (m_cursorY + bmpH > newH) {
            newH *= 2;
        }
        if (newW != m_atlasWidth || newH != m_atlasHeight) {
            std::vector<unsigned char> newData(static_cast<size_t>(newW) * static_cast<size_t>(newH), 0);
            for (int row = 0; row < m_atlasHeight; ++row) {
                std::memcpy(&newData[static_cast<size_t>(row) * static_cast<size_t>(newW)],
                            &m_pixelData[static_cast<size_t>(row) * static_cast<size_t>(m_atlasWidth)],
                            static_cast<size_t>(m_atlasWidth));
            }
            m_pixelData = std::move(newData);
            m_atlasWidth = newW;
            m_atlasHeight = newH;
        }
    }

    const float invW = 1.0f / static_cast<float>(m_atlasWidth);
    const float invH = 1.0f / static_cast<float>(m_atlasHeight);

    GlyphInfo info{};
    info.uvMinX = static_cast<float>(m_cursorX) * invW;
    info.uvMinY = static_cast<float>(m_cursorY) * invH;
    info.uvMaxX = static_cast<float>(m_cursorX + bmpW) * invW;
    info.uvMaxY = static_cast<float>(m_cursorY + bmpH) * invH;
    info.bitmapWidth = bmpW;
    info.bitmapHeight = bmpH;
    info.bearingX = static_cast<int>(slot->bitmap_left);
    info.bearingY = static_cast<int>(slot->bitmap_top);
    info.advanceX = static_cast<int>(slot->advance.x);

    // Store rows in reverse order: FreeType row 0 (glyph top) → highest atlas row,
    // so that OpenGL's V=0 (texel row 0) holds the glyph bottom and V=1 holds the glyph top.
    const unsigned char* src = slot->bitmap.buffer;
    const int pitch = static_cast<int>(slot->bitmap.pitch);
    const int rowStride = pitch < 0 ? -pitch : pitch;
    if (!src || rowStride < bmpW) {
        m_glyphs[codepoint] = info;
        return;
    }
    const unsigned char* srcTop = pitch < 0
        ? src + static_cast<std::size_t>(bmpH - 1) * static_cast<std::size_t>(rowStride)
        : src;
    for (int row = 0; row < bmpH; ++row) {
        const int dstRow = m_cursorY + (bmpH - 1 - row);
        const size_t dstOffset = static_cast<size_t>(dstRow) * static_cast<size_t>(m_atlasWidth) + static_cast<size_t>(m_cursorX);
        const unsigned char* srcRow = pitch < 0
            ? srcTop - static_cast<std::size_t>(row) * static_cast<std::size_t>(rowStride)
            : srcTop + static_cast<std::size_t>(row) * static_cast<std::size_t>(rowStride);
        std::memcpy(&m_pixelData[dstOffset], srcRow, static_cast<size_t>(bmpW));
    }

    m_glyphs[codepoint] = info;
    m_cursorX += bmpW + 1;
    m_rowHeight = std::max(m_rowHeight, bmpH);
    m_dirty = true;
}

void GlyphAtlas::uploadAtlas() const
{
    if (m_pixelData.empty() || m_atlasWidth <= 0 || m_atlasHeight <= 0) {
        return;
    }

    if (m_texture == 0) {
        glGenTextures(1, &m_texture);
    }

    GLint previousTexture = 0;
    GLint previousUnpackAlignment = 4;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousUnpackAlignment);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, m_atlasWidth, m_atlasHeight, 0,
                 GL_RED, GL_UNSIGNED_BYTE, m_pixelData.data());

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    GLint swizzle[] = {GL_RED, GL_RED, GL_RED, GL_RED};
    glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzle);

    glPixelStorei(GL_UNPACK_ALIGNMENT, previousUnpackAlignment);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
    m_dirty = false;
}
