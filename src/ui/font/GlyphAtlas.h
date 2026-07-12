#pragma once

#include "../../renderer/rhi/RhiHandles.h"
#include "../../renderer/rhi/RhiTypes.h"

#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <vector>

class RhiCommandList;
class RhiDevice;

struct GlyphInfo {
    float uvMinX = 0.0f;
    float uvMinY = 0.0f;
    float uvMaxX = 0.0f;
    float uvMaxY = 0.0f;
    int atlasX = 0;
    int atlasY = 0;
    int bitmapWidth = 0;
    int bitmapHeight = 0;
    int bearingX = 0;
    int bearingY = 0;
    int advanceX = 0;
};

class GlyphAtlas {
public:
    bool init(RhiDevice& rhiDevice, const char* ttfPath, int pixelHeight = 32);
    void shutdown();

    // Rasterizes every codepoint in the UTF-8 input into CPU atlas storage.
    // Returns false when the input is malformed or FreeType cannot load a glyph.
    bool ensureGlyphs(std::string_view text);
    [[nodiscard]] const GlyphInfo* findGlyph(uint32_t codepoint) const;

    // Records the complete dirty atlas upload before an active rendering scope.
    bool prepareUpload(RhiCommandList& commandList);

    [[nodiscard]] RhiTextureHandle textureHandle() const { return m_texture; }
    [[nodiscard]] int lineHeight() const { return m_lineHeight; }
    [[nodiscard]] int descent() const { return m_descent; }
    [[nodiscard]] int pixelHeight() const { return m_pixelHeight; }
    [[nodiscard]] int atlasWidth() const { return m_atlasWidth; }
    [[nodiscard]] int atlasHeight() const { return m_atlasHeight; }
    [[nodiscard]] uint64_t revision() const { return m_revision; }
    [[nodiscard]] bool requiresTextureRecreation() const {
        return m_texture.isValid() &&
               (m_uploadedWidth != m_atlasWidth || m_uploadedHeight != m_atlasHeight);
    }

private:
    bool rasterizeGlyph(uint32_t codepoint);
    bool resizeAtlas(int requiredWidth, int requiredHeight);
    void refreshGlyphUvs();

    RhiDevice* m_rhiDevice = nullptr;
    void* m_library = nullptr;
    void* m_face = nullptr;
    RhiBufferHandle m_stagingBuffer;
    RhiTextureHandle m_texture;
    RhiResourceState m_textureState = RhiResourceState::Undefined;
    int m_uploadedWidth = 0;
    int m_uploadedHeight = 0;
    int m_pixelHeight = 32;
    int m_lineHeight = 0;
    int m_descent = 0;
    int m_atlasWidth = 512;
    int m_atlasHeight = 512;
    int m_cursorX = 0;
    int m_cursorY = 0;
    int m_rowHeight = 0;
    bool m_dirty = false;
    uint64_t m_revision = 0;
    std::vector<unsigned char> m_pixelData;
    std::unordered_map<uint32_t, GlyphInfo> m_glyphs;
};
