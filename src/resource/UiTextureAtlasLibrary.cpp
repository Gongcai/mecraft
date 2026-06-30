#include "UiTextureAtlasLibrary.h"

#include "BlockIconAtlasBuilder.h"
#include "TextureAtlasBuilders.h"

#include <glad/glad.h>

#include <stdexcept>
#include <utility>

void UiTextureAtlasLibrary::shutdown() {
    if (m_blockIconAtlas.textureID != 0) {
        glDeleteTextures(1, &m_blockIconAtlas.textureID);
        m_blockIconAtlas.textureID = 0;
    }

    if (m_itemTextureAtlas.textureID != 0) {
        glDeleteTextures(1, &m_itemTextureAtlas.textureID);
        m_itemTextureAtlas.textureID = 0;
    }

    if (m_hudIconAtlas.textureID != 0) {
        glDeleteTextures(1, &m_hudIconAtlas.textureID);
        m_hudIconAtlas.textureID = 0;
    }

    m_itemTexturePixels.clear();
    m_itemTextureIndices.clear();
    m_hudIconIndices.clear();
}

void UiTextureAtlasLibrary::buildBlockIconAtlas(const int iconSize, const BlockTextureLibrary& blockTextures) {
    const TextureAtlas& blockAtlas = blockTextures.atlas();
    const std::vector<unsigned char>& blockAtlasPixels = blockTextures.atlasPixels();
    if (blockAtlas.textureID == 0 || blockAtlasPixels.empty() || blockAtlas.tileSize <= 0 || blockAtlas.tilesPerRow <= 0) {
        throw std::runtime_error("Block icon atlas requires the block texture atlas to be built first");
    }

    if (m_blockIconAtlas.textureID != 0) {
        glDeleteTextures(1, &m_blockIconAtlas.textureID);
        m_blockIconAtlas.textureID = 0;
    }

    m_blockIconAtlas = resource::buildBlockIconAtlas(iconSize, blockAtlas, blockAtlasPixels, blockTextures);
}

const TextureAtlas& UiTextureAtlasLibrary::blockIconAtlas() const {
    return m_blockIconAtlas;
}

void UiTextureAtlasLibrary::buildItemTextureAtlas(const std::string& directory,
                                                 const int tileSize,
                                                 const BlockTextureCatalog& catalog) {
    if (m_itemTextureAtlas.textureID != 0) {
        glDeleteTextures(1, &m_itemTextureAtlas.textureID);
        m_itemTextureAtlas.textureID = 0;
    }

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
    if (m_hudIconAtlas.textureID != 0) {
        glDeleteTextures(1, &m_hudIconAtlas.textureID);
        m_hudIconAtlas.textureID = 0;
    }

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
