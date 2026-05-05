#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <glad/glad.h>

struct GlyphInfo {
    float uvMinX = 0.0f;
    float uvMinY = 0.0f;
    float uvMaxX = 0.0f;
    float uvMaxY = 0.0f;
    int bitmapWidth = 0;
    int bitmapHeight = 0;
    int bearingX = 0;
    int bearingY = 0;
    int advanceX = 0;
};

class GlyphAtlas {
public:
    bool init(const char* ttfPath, int pixelHeight = 32);
    void shutdown();

    const GlyphInfo& getGlyph(uint32_t codepoint) const;
    GLuint getTexture() const { return m_texture; }
    int getLineHeight() const { return m_lineHeight; }
    int getDescent() const { return m_descent; }
    int getPixelHeight() const { return m_pixelHeight; }

private:
    void rasterizeGlyph(uint32_t codepoint) const;
    void uploadAtlas() const;

    void* m_library = nullptr;
    void* m_face = nullptr;
    mutable GLuint m_texture = 0;
    int m_pixelHeight = 32;
    int m_lineHeight = 0;
    int m_descent = 0;
    mutable int m_atlasWidth = 512;
    mutable int m_atlasHeight = 512;
    mutable int m_cursorX = 0;
    mutable int m_cursorY = 0;
    mutable int m_rowHeight = 0;
    mutable bool m_dirty = false;
    mutable std::vector<unsigned char> m_pixelData;
    mutable std::unordered_map<uint32_t, GlyphInfo> m_glyphs;
};
