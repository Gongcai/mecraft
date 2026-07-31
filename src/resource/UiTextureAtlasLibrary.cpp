#include "UiTextureAtlasLibrary.h"

#include "BlockIconAtlasBuilder.h"
#include "TextureAtlasBuilders.h"
#include "../Diagnostics.h"
#include "renderer/rhi/RhiDevice.h"

#include <cassert>
#include <cstdio>
#include <utility>

void UiTextureAtlasLibrary::init(RhiDevice& rhiDevice, RhiCommandListPool& commandListPool) {
    m_rhiDevice = &rhiDevice;
    m_commandListPool = &commandListPool;
}

void UiTextureAtlasLibrary::deleteTextureAtlas(TextureAtlas& atlas) {
    if (m_rhiDevice != nullptr && atlas.texture.isValid()) {
        m_rhiDevice->destroyTexture(atlas.texture);
    }
    atlas = {};
}

void UiTextureAtlasLibrary::shutdown() {
    deleteTextureAtlas(m_blockIconAtlas);
    deleteTextureAtlas(m_itemTextureAtlas);
    deleteTextureAtlas(m_hudIconAtlas);

    m_itemTexturePixels.clear();
    m_itemTextureIndices.clear();
    m_hudIconIndices.clear();
    m_rhiDevice = nullptr;
    m_commandListPool = nullptr;
}

void UiTextureAtlasLibrary::buildBlockIconAtlas(const int iconSize, const BlockTextureLibrary& blockTextures) {
    const TextureAtlas& blockAtlas = blockTextures.atlas();
    const std::vector<unsigned char>& blockAtlasPixels = blockTextures.atlasPixels();
    if (!blockAtlas.texture.isValid() || blockAtlasPixels.empty() || blockAtlas.tileSize <= 0 ||
        blockAtlas.tilesPerRow <= 0 || m_rhiDevice == nullptr) {
        MECRAFT_LOG_FPRINTF(stderr, "[Resource] Block icon atlas requires the block texture atlas to be built first\n");
        return;
    }

    deleteTextureAtlas(m_blockIconAtlas);

    m_blockIconAtlas =
        resource::buildBlockIconAtlas(iconSize, blockAtlas, blockAtlasPixels, blockTextures, *m_rhiDevice);
}

const TextureAtlas& UiTextureAtlasLibrary::blockIconAtlas() const {
    return m_blockIconAtlas;
}

void UiTextureAtlasLibrary::buildItemTextureAtlas(const std::string& directory, const int tileSize,
                                                  const BlockTextureCatalog& catalog) {
    assert(m_rhiDevice != nullptr);
    deleteTextureAtlas(m_itemTextureAtlas);

    resource::IndexedTextureAtlas atlas =
        resource::buildItemTextureAtlas(directory, tileSize, catalog, *m_rhiDevice, *m_commandListPool);
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
    assert(m_rhiDevice != nullptr);
    deleteTextureAtlas(m_hudIconAtlas);

    resource::IndexedTextureAtlas atlas =
        resource::buildHudIconAtlas(directory, iconSize, *m_rhiDevice, *m_commandListPool);
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
