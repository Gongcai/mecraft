#include "UiTextureAtlasLibrary.h"

#include "BlockIconAtlasBuilder.h"
#include "RhiTextureResourceUtils.h"
#include "TextureAtlasBuilders.h"
#include "../Diagnostics.h"
#include "renderer/rhi/gl/GlRhiTextureRegistry.h"

#include <glad/glad.h>

#include <cstdio>
#include <utility>

namespace {

[[nodiscard]] GLuint textureId(const TextureAtlas& atlas) {
    return static_cast<GLuint>(renderer::rhi::gl::textureId(atlas.texture));
}

void deleteTextureAtlas(TextureAtlas& atlas) {
    const GLuint nativeTexture = textureId(atlas);
    resource::unregisterTextureAtlas(atlas);
    if (nativeTexture != 0) {
        glDeleteTextures(1, &nativeTexture);
    }
    atlas = {};
}

} // namespace

void UiTextureAtlasLibrary::shutdown() {
    deleteTextureAtlas(m_blockIconAtlas);
    deleteTextureAtlas(m_itemTextureAtlas);
    deleteTextureAtlas(m_hudIconAtlas);

    m_itemTexturePixels.clear();
    m_itemTextureIndices.clear();
    m_hudIconIndices.clear();
}

void UiTextureAtlasLibrary::buildBlockIconAtlas(const int iconSize, const BlockTextureLibrary& blockTextures) {
    const TextureAtlas& blockAtlas = blockTextures.atlas();
    const std::vector<unsigned char>& blockAtlasPixels = blockTextures.atlasPixels();
    if (textureId(blockAtlas) == 0 || blockAtlasPixels.empty() || blockAtlas.tileSize <= 0 || blockAtlas.tilesPerRow <= 0) {
        MECRAFT_LOG_FPRINTF(stderr, "[Resource] Block icon atlas requires the block texture atlas to be built first\n");
        return;
    }

    deleteTextureAtlas(m_blockIconAtlas);

    m_blockIconAtlas = resource::buildBlockIconAtlas(iconSize, blockAtlas, blockAtlasPixels, blockTextures);
}

const TextureAtlas& UiTextureAtlasLibrary::blockIconAtlas() const {
    return m_blockIconAtlas;
}

void UiTextureAtlasLibrary::buildItemTextureAtlas(const std::string& directory,
                                                 const int tileSize,
                                                 const BlockTextureCatalog& catalog) {
    deleteTextureAtlas(m_itemTextureAtlas);

    resource::IndexedTextureAtlas atlas = resource::buildItemTextureAtlas(directory, tileSize, catalog);
    m_itemTextureAtlas = atlas.atlas;
    m_itemTexturePixels = std::move(atlas.pixels);
    m_itemTextureIndices = std::move(atlas.indices);
}

const TextureAtlas& UiTextureAtlasLibrary::itemTextureAtlas() const {
    return m_itemTextureAtlas;
}

int UiTextureAtlasLibrary::itemTextureIndex(const std::string& textureName) const {
    const auto it = m_itemTextureIndices.find(textureName);
    if (it == m_itemTextureIndices.end()) {
        return -1;
    }
    return it->second;
}

const std::vector<unsigned char>& UiTextureAtlasLibrary::itemTexturePixels() const {
    return m_itemTexturePixels;
}

void UiTextureAtlasLibrary::buildHudIconAtlas(const std::string& directory, const int iconSize) {
    deleteTextureAtlas(m_hudIconAtlas);

    resource::IndexedTextureAtlas atlas = resource::buildHudIconAtlas(directory, iconSize);
    m_hudIconAtlas = atlas.atlas;
    m_hudIconIndices = std::move(atlas.indices);
}

const TextureAtlas& UiTextureAtlasLibrary::hudIconAtlas() const {
    return m_hudIconAtlas;
}

int UiTextureAtlasLibrary::hudIconIndex(const std::string& iconName) const {
    const auto it = m_hudIconIndices.find(iconName);
    if (it == m_hudIconIndices.end()) {
        return -1;
    }
    return it->second;
}
