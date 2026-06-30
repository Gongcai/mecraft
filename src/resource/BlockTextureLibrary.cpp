#include "BlockTextureLibrary.h"

#include "BlockTextureArrayBuilder.h"
#include "TextureAtlasBuilders.h"
#include "../Diagnostics.h"

#include <glad/glad.h>

#include <iostream>
#include <stdexcept>
#include <utility>

void BlockTextureLibrary::shutdown() {
    if (m_atlas.textureID != 0) {
        glDeleteTextures(1, &m_atlas.textureID);
        m_atlas.textureID = 0;
    }

    if (m_textureArray.textureID != 0) {
        glDeleteTextures(1, &m_textureArray.textureID);
        m_textureArray.textureID = 0;
    }

    m_atlasPixels.clear();
    m_textureArrayLayers.clear();
    m_catalog.clear();
    m_arrayLayerToAtlasTile.clear();
}

void BlockTextureLibrary::loadCatalog(const std::string& textureConfigPath) {
    m_catalog.load(textureConfigPath);
}

void BlockTextureLibrary::buildAtlas(const std::string& directory, const int tileSize) {
    if (m_atlas.textureID != 0) {
        glDeleteTextures(1, &m_atlas.textureID);
        m_atlas.textureID = 0;
    }

    resource::IndexedTextureAtlas atlasResult = resource::buildBlockTextureAtlas(directory, tileSize, m_catalog);
    m_atlas = atlasResult.atlas;
    m_atlasPixels = std::move(atlasResult.pixels);

    m_sampler.refreshAnisotropySupport();
    m_sampler.applyToTexture2D(m_atlas.textureID);
}

void BlockTextureLibrary::buildTextureArray(const std::string& directory, const int tileSize) {
    if (m_textureArray.textureID != 0) {
        glDeleteTextures(1, &m_textureArray.textureID);
        m_textureArray.textureID = 0;
    }

    resource::BlockTextureArray textureArrayResult = resource::buildBlockTextureArray(directory, tileSize, m_catalog);
    m_textureArray = textureArrayResult.textureArray;
    m_textureArrayLayers = std::move(textureArrayResult.layers);
    m_arrayLayerToAtlasTile = std::move(textureArrayResult.layerToAtlasTile);

    m_sampler.refreshAnisotropySupport();
    m_sampler.applyToTexture2DArray(m_textureArray.textureID);
}

const TextureAtlas& BlockTextureLibrary::atlas() const {
    return m_atlas;
}

const TextureArray& BlockTextureLibrary::textureArray() const {
    return m_textureArray;
}

const std::vector<unsigned char>& BlockTextureLibrary::atlasPixels() const {
    return m_atlasPixels;
}

const BlockTextureCatalog& BlockTextureLibrary::catalog() const {
    return m_catalog;
}

int BlockTextureLibrary::textureArrayLayer(const std::string& name) const {
    const auto it = m_textureArrayLayers.find(name);
    if (it != m_textureArrayLayers.end()) {
        return it->second;
    }
    throw std::runtime_error("Unknown block texture array layer: " + name);
}

TextureAnimationInfo BlockTextureLibrary::textureAnimation(const std::string& name) const {
    const BlockTextureCatalogEntry* catalogEntry = m_catalog.find(name);
    if (catalogEntry != nullptr) {
        TextureAnimationInfo info = catalogEntry->animation;
        if (info.firstLayer == 0) {
            info.firstLayer = textureArrayLayer(name);
        }
        return info;
    }

    TextureAnimationInfo info;
    info.firstLayer = textureArrayLayer(name);
    info.frameCount = 1;
    info.fps = 0.0f;
    info.isAnimated = false;
    return info;
}

ResourceTextureTint BlockTextureLibrary::textureTint(const std::string& name) const {
    return m_catalog.tintFor(name);
}

int BlockTextureLibrary::arrayLayerToAtlasTile(const int arrayLayer) const {
    const auto it = m_arrayLayerToAtlasTile.find(arrayLayer);
    if (it != m_arrayLayerToAtlasTile.end()) {
        return it->second;
    }
#ifdef MECRAFT_DEBUG
    MECRAFT_LOG_STREAM(std::cerr << "[BlockTextureLibrary] arrayLayerToAtlasTile: unmapped layer " << arrayLayer << "\n");
#endif
    return -1;
}

void BlockTextureLibrary::setAnisotropy(const float anisotropy) {
    m_sampler.setAnisotropy(anisotropy);

    if (m_atlas.textureID != 0) {
        m_sampler.applyToTexture2D(m_atlas.textureID);
    }

    if (m_textureArray.textureID != 0) {
        m_sampler.applyToTexture2DArray(m_textureArray.textureID);
    }
}

float BlockTextureLibrary::anisotropy() const {
    return m_sampler.anisotropy();
}

float BlockTextureLibrary::maxAnisotropy() const {
    return m_sampler.maxAnisotropy();
}
