#include "GlyphAtlas.h"

#include "../../renderer/rhi/RhiCommandList.h"
#include "../../renderer/rhi/RhiDevice.h"
#include "../../renderer/rhi/RhiResources.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>

#include <ft2build.h>
#include FT_FREETYPE_H

namespace {

bool decodeUtf8Codepoint(const char*& cursor, const char* end, uint32_t& codepoint) {
    if (cursor >= end) {
        return false;
    }
    const auto first = static_cast<unsigned char>(*cursor);
    if (first < 0x80u) {
        codepoint = first;
        ++cursor;
        return true;
    }

    uint32_t value = 0u;
    uint32_t minimum = 0u;
    int continuationCount = 0;
    if ((first & 0xe0u) == 0xc0u) {
        value = first & 0x1fu;
        minimum = 0x80u;
        continuationCount = 1;
    } else if ((first & 0xf0u) == 0xe0u) {
        value = first & 0x0fu;
        minimum = 0x800u;
        continuationCount = 2;
    } else if ((first & 0xf8u) == 0xf0u) {
        value = first & 0x07u;
        minimum = 0x10000u;
        continuationCount = 3;
    } else {
        return false;
    }
    if (end - cursor <= continuationCount) {
        return false;
    }
    for (int index = 1; index <= continuationCount; ++index) {
        const auto byte = static_cast<unsigned char>(cursor[index]);
        if ((byte & 0xc0u) != 0x80u) {
            return false;
        }
        value = (value << 6u) | (byte & 0x3fu);
    }
    if (value < minimum || value > 0x10ffffu ||
        (value >= 0xd800u && value <= 0xdfffu)) {
        return false;
    }
    cursor += continuationCount + 1;
    codepoint = value;
    return true;
}

} // namespace

bool GlyphAtlas::init(RhiDevice& rhiDevice, const char* ttfPath, const int pixelHeight) {
    shutdown();
    if (ttfPath == nullptr || pixelHeight <= 0) {
        return false;
    }

    FT_Library library = nullptr;
    if (FT_Init_FreeType(&library) != 0) {
        return false;
    }
    FT_Face face = nullptr;
    if (FT_New_Face(library, ttfPath, 0, &face) != 0) {
        FT_Done_FreeType(library);
        return false;
    }
    if (FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(pixelHeight)) != 0) {
        FT_Done_Face(face);
        FT_Done_FreeType(library);
        return false;
    }

    m_rhiDevice = &rhiDevice;
    m_library = library;
    m_face = face;
    m_pixelHeight = pixelHeight;
    m_lineHeight = static_cast<int>(face->size->metrics.height >> 6);
    m_descent = -static_cast<int>(face->size->metrics.descender >> 6);
    m_atlasWidth = 512;
    m_atlasHeight = 512;
    m_pixelData.assign(static_cast<size_t>(m_atlasWidth) * static_cast<size_t>(m_atlasHeight), 0u);
    return true;
}

void GlyphAtlas::shutdown() {
    if (m_rhiDevice != nullptr && m_stagingBuffer.isValid()) {
        m_rhiDevice->destroyBuffer(m_stagingBuffer);
    }
    if (m_rhiDevice != nullptr && m_texture.isValid()) {
        m_rhiDevice->destroyTexture(m_texture);
    }
    m_stagingBuffer = {};
    m_texture = {};
    m_textureState = RhiResourceState::Undefined;
    m_uploadedWidth = 0;
    m_uploadedHeight = 0;
    if (m_face != nullptr) {
        FT_Done_Face(static_cast<FT_Face>(m_face));
    }
    if (m_library != nullptr) {
        FT_Done_FreeType(static_cast<FT_Library>(m_library));
    }
    m_rhiDevice = nullptr;
    m_library = nullptr;
    m_face = nullptr;
    m_cursorX = 0;
    m_cursorY = 0;
    m_rowHeight = 0;
    m_dirty = false;
    m_revision = 0;
    m_glyphs.clear();
    m_pixelData.clear();
}

bool GlyphAtlas::ensureGlyphs(const std::string_view text) {
    const char* cursor = text.data();
    const char* const end = cursor + text.size();
    while (cursor < end) {
        if (*cursor == '\n' || *cursor == '\r') {
            ++cursor;
            continue;
        }
        uint32_t codepoint = 0u;
        if (!decodeUtf8Codepoint(cursor, end, codepoint)) {
            return false;
        }
        if (m_glyphs.find(codepoint) == m_glyphs.end() && !rasterizeGlyph(codepoint)) {
            return false;
        }
    }
    return true;
}

const GlyphInfo* GlyphAtlas::findGlyph(const uint32_t codepoint) const {
    const auto it = m_glyphs.find(codepoint);
    return it == m_glyphs.end() ? nullptr : &it->second;
}

bool GlyphAtlas::rasterizeGlyph(const uint32_t codepoint) {
    if (m_face == nullptr) {
        return false;
    }
    auto* const face = static_cast<FT_Face>(m_face);
    if (FT_Load_Char(face, static_cast<FT_ULong>(codepoint), FT_LOAD_RENDER) != 0) {
        return false;
    }

    const FT_GlyphSlot slot = face->glyph;
    const int bitmapWidth = static_cast<int>(slot->bitmap.width);
    const int bitmapHeight = static_cast<int>(slot->bitmap.rows);
    if (bitmapWidth < 0 || bitmapHeight < 0) {
        return false;
    }

    if (bitmapWidth > 0 && bitmapHeight > 0) {
        if (m_cursorX + bitmapWidth > m_atlasWidth) {
            m_cursorX = 0;
            m_cursorY += m_rowHeight + 1;
            m_rowHeight = 0;
        }
        if (!resizeAtlas(m_cursorX + bitmapWidth, m_cursorY + bitmapHeight)) {
            return false;
        }
    }

    GlyphInfo info;
    info.atlasX = m_cursorX;
    info.atlasY = m_cursorY;
    info.bitmapWidth = bitmapWidth;
    info.bitmapHeight = bitmapHeight;
    info.bearingX = static_cast<int>(slot->bitmap_left);
    info.bearingY = static_cast<int>(slot->bitmap_top);
    info.advanceX = static_cast<int>(slot->advance.x);

    if (bitmapWidth > 0 && bitmapHeight > 0) {
        const unsigned char* source = slot->bitmap.buffer;
        const int pitch = static_cast<int>(slot->bitmap.pitch);
        const int rowStride = pitch < 0 ? -pitch : pitch;
        if (source == nullptr || rowStride < bitmapWidth) {
            return false;
        }
        const unsigned char* sourceTop = pitch < 0
            ? source + static_cast<size_t>(bitmapHeight - 1) * static_cast<size_t>(rowStride)
            : source;
        for (int row = 0; row < bitmapHeight; ++row) {
            const int destinationRow = m_cursorY + bitmapHeight - 1 - row;
            const size_t destinationOffset =
                static_cast<size_t>(destinationRow) * static_cast<size_t>(m_atlasWidth) +
                static_cast<size_t>(m_cursorX);
            const unsigned char* sourceRow = pitch < 0
                ? sourceTop - static_cast<size_t>(row) * static_cast<size_t>(rowStride)
                : sourceTop + static_cast<size_t>(row) * static_cast<size_t>(rowStride);
            std::memcpy(m_pixelData.data() + destinationOffset, sourceRow,
                        static_cast<size_t>(bitmapWidth));
        }
        m_cursorX += bitmapWidth + 1;
        m_rowHeight = std::max(m_rowHeight, bitmapHeight);
    }

    m_glyphs.emplace(codepoint, info);
    refreshGlyphUvs();
    m_dirty = true;
    ++m_revision;
    return true;
}

bool GlyphAtlas::resizeAtlas(const int requiredWidth, const int requiredHeight) {
    int newWidth = m_atlasWidth;
    int newHeight = m_atlasHeight;
    while (requiredWidth > newWidth) {
        if (newWidth > std::numeric_limits<int>::max() / 2) {
            return false;
        }
        newWidth *= 2;
    }
    while (requiredHeight > newHeight) {
        if (newHeight > std::numeric_limits<int>::max() / 2) {
            return false;
        }
        newHeight *= 2;
    }
    if (newWidth == m_atlasWidth && newHeight == m_atlasHeight) {
        return true;
    }

    std::vector<unsigned char> resized(
        static_cast<size_t>(newWidth) * static_cast<size_t>(newHeight), 0u);
    for (int row = 0; row < m_atlasHeight; ++row) {
        std::memcpy(resized.data() + static_cast<size_t>(row) * static_cast<size_t>(newWidth),
                    m_pixelData.data() + static_cast<size_t>(row) * static_cast<size_t>(m_atlasWidth),
                    static_cast<size_t>(m_atlasWidth));
    }
    m_pixelData = std::move(resized);
    m_atlasWidth = newWidth;
    m_atlasHeight = newHeight;
    refreshGlyphUvs();
    return true;
}

void GlyphAtlas::refreshGlyphUvs() {
    const float inverseWidth = 1.0f / static_cast<float>(m_atlasWidth);
    const float inverseHeight = 1.0f / static_cast<float>(m_atlasHeight);
    for (auto& entry : m_glyphs) {
        GlyphInfo& glyph = entry.second;
        glyph.uvMinX = static_cast<float>(glyph.atlasX) * inverseWidth;
        glyph.uvMinY = static_cast<float>(glyph.atlasY) * inverseHeight;
        glyph.uvMaxX = static_cast<float>(glyph.atlasX + glyph.bitmapWidth) * inverseWidth;
        glyph.uvMaxY = static_cast<float>(glyph.atlasY + glyph.bitmapHeight) * inverseHeight;
    }
}

bool GlyphAtlas::prepareUpload(RhiCommandList& commandList) {
    if (m_rhiDevice == nullptr || m_pixelData.empty()) {
        return false;
    }
    if (!m_dirty && m_texture.isValid()) {
        return true;
    }

    const bool dimensionsChanged = m_uploadedWidth != m_atlasWidth ||
                                   m_uploadedHeight != m_atlasHeight;
    if (m_texture.isValid() && dimensionsChanged) {
        m_rhiDevice->destroyTexture(m_texture);
        m_texture = {};
        m_textureState = RhiResourceState::Undefined;
    }
    const bool requiresTexture = !m_texture.isValid();
    if (requiresTexture) {
        RhiTextureDesc textureDesc;
        textureDesc.debugName = "UiText.GlyphAtlas";
        textureDesc.format = RhiTextureFormat::R8Unorm;
        textureDesc.width = static_cast<uint32_t>(m_atlasWidth);
        textureDesc.height = static_cast<uint32_t>(m_atlasHeight);
        textureDesc.usage = rhiFlag(RhiTextureUsage::Sampled) |
                            rhiFlag(RhiTextureUsage::TransferDst);
        textureDesc.memoryCategory = RhiMemoryCategory::Texture;
        m_texture = m_rhiDevice->createTexture(textureDesc, nullptr);
        if (!m_texture.isValid()) {
            return false;
        }
        m_textureState = RhiResourceState::Undefined;
        m_uploadedWidth = m_atlasWidth;
        m_uploadedHeight = m_atlasHeight;
    }

    if (m_stagingBuffer.isValid()) {
        m_rhiDevice->destroyBuffer(m_stagingBuffer);
        m_stagingBuffer = {};
    }

    RhiBufferDesc stagingDesc;
    stagingDesc.debugName = "UiText.GlyphAtlasStaging";
    stagingDesc.size = m_pixelData.size();
    stagingDesc.usage = rhiFlag(RhiBufferUsage::TransferSrc) |
                        rhiFlag(RhiBufferUsage::TransferDst);
    stagingDesc.memoryUsage = RhiMemoryUsage::CpuToGpu;
    stagingDesc.initialState = RhiResourceState::TransferSrc;
    stagingDesc.memoryCategory = RhiMemoryCategory::Transient;
    m_stagingBuffer =
        m_rhiDevice->createBuffer(stagingDesc, m_pixelData.data(), m_pixelData.size());
    if (!m_stagingBuffer.isValid()) {
        return false;
    }

    commandList.textureBarrier({m_texture, m_textureState, RhiResourceState::TransferDst});
    RhiBufferTextureCopy copy;
    copy.srcBuffer = m_stagingBuffer;
    copy.dstTexture = m_texture;
    copy.width = static_cast<uint32_t>(m_atlasWidth);
    copy.height = static_cast<uint32_t>(m_atlasHeight);
    commandList.copyBufferToTexture(copy);
    commandList.textureBarrier({m_texture, RhiResourceState::TransferDst,
                                RhiResourceState::ShaderRead});
    m_textureState = RhiResourceState::ShaderRead;
    m_dirty = false;
    return true;
}
