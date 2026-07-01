#include "BlockTextureLibrary.h"

#include "BlockTextureArrayBuilder.h"
#include "TextureAtlasBuilders.h"
#include "../Diagnostics.h"

#include <glad/glad.h>

#include <iostream>
#include <stdexcept>
#include <utility>

namespace {

constexpr int kBlockPreviewAtlasTileSize = 64;

} // namespace

void BlockTextureLibrary::deleteTextureArray(TextureArray& textureArray) {
    if (textureArray.textureID != 0) {
        glDeleteTextures(1, &textureArray.textureID);
        textureArray.textureID = 0;
    }
    textureArray.tileSize = 0;
    textureArray.layerCount = 0;
}

void BlockTextureLibrary::applySamplerToTextureArrays() {
    if (m_textureArray.textureID != 0) {
        m_sampler.applyToTexture2DArray(m_textureArray.textureID);
    }
    if (m_normalTextureArray.textureID != 0) {
        m_sampler.applyToTexture2DArray(m_normalTextureArray.textureID);
    }
    if (m_specularTextureArray.textureID != 0) {
        m_sampler.applyToTexture2DArray(m_specularTextureArray.textureID);
    }
}

void BlockTextureLibrary::shutdown() {
    if (m_atlas.textureID != 0) {
        glDeleteTextures(1, &m_atlas.textureID);
        m_atlas.textureID = 0;
    }

    deleteTextureArray(m_textureArray);
    deleteTextureArray(m_normalTextureArray);
    deleteTextureArray(m_specularTextureArray);

    m_atlasPixels.clear();
    m_textureArrayLayers.clear();
    m_catalog.clear();
    m_manifest.clear();
    m_arrayLayerToAtlasTile.clear();
    m_hasNormalMaps = false;
    m_hasSpecularMaps = false;
}

void BlockTextureLibrary::loadCatalog(const std::string& textureConfigPath) {
    m_catalog.load(textureConfigPath);
}

void BlockTextureLibrary::buildTextures(const std::string& directory, const int tileSize) {
    buildTextures(std::vector<std::string>{directory}, tileSize);
}

void BlockTextureLibrary::buildTextures(const std::vector<std::string>& directories, const int tileSize) {
    resource::BlockTextureSourceSet sourceSet;
    sourceSet.textureDirectories = directories;
    buildTextures(sourceSet, tileSize);
}

void BlockTextureLibrary::buildTextures(const resource::BlockTextureSourceSet& sourceSet, const int tileSize) {
    if (m_atlas.textureID != 0) {
        glDeleteTextures(1, &m_atlas.textureID);
        m_atlas.textureID = 0;
    }
    deleteTextureArray(m_textureArray);
    deleteTextureArray(m_normalTextureArray);
    deleteTextureArray(m_specularTextureArray);

    m_manifest = resource::buildBlockTextureManifest(sourceSet);

    resource::BlockTextureTileSizes tileSizes;
    if (tileSize > 0) {
        tileSizes.albedo = tileSize;
        tileSizes.normal = tileSize;
        tileSizes.specular = tileSize;
    } else {
        tileSizes = resource::inferBlockTextureTileSizes(m_manifest, m_catalog);
    }

    resource::IndexedTextureAtlas atlasResult =
        resource::buildBlockTextureAtlas(m_manifest, kBlockPreviewAtlasTileSize, m_catalog);
    m_atlas = atlasResult.atlas;
    m_atlasPixels = std::move(atlasResult.pixels);

    resource::BlockTextureArraySet textureArrayResult = resource::buildBlockTextureArraySet(m_manifest, tileSizes, m_catalog);
    m_textureArray = textureArrayResult.albedoArray;
    m_normalTextureArray = textureArrayResult.normalArray;
    m_specularTextureArray = textureArrayResult.specularArray;
    m_textureArrayLayers = std::move(textureArrayResult.layers);
    m_arrayLayerToAtlasTile = std::move(textureArrayResult.layerToAtlasTile);
    m_hasNormalMaps = textureArrayResult.hasNormalMaps;
    m_hasSpecularMaps = textureArrayResult.hasSpecularMaps;

    m_sampler.refreshAnisotropySupport();
    m_sampler.applyToTexture2D(m_atlas.textureID);
    applySamplerToTextureArrays();
}

void BlockTextureLibrary::buildAtlas(const std::string& directory, const int tileSize) {
    if (m_atlas.textureID != 0) {
        glDeleteTextures(1, &m_atlas.textureID);
        m_atlas.textureID = 0;
    }

    m_manifest = resource::buildBlockTextureManifest(directory);
    resource::IndexedTextureAtlas atlasResult = resource::buildBlockTextureAtlas(m_manifest, tileSize, m_catalog);
    m_atlas = atlasResult.atlas;
    m_atlasPixels = std::move(atlasResult.pixels);

    m_sampler.refreshAnisotropySupport();
    m_sampler.applyToTexture2D(m_atlas.textureID);
}

void BlockTextureLibrary::buildTextureArray(const std::string& directory, const int tileSize) {
    deleteTextureArray(m_textureArray);
    deleteTextureArray(m_normalTextureArray);
    deleteTextureArray(m_specularTextureArray);

    m_manifest = resource::buildBlockTextureManifest(directory);
    resource::BlockTextureArraySet textureArrayResult = resource::buildBlockTextureArraySet(m_manifest, tileSize, m_catalog);
    m_textureArray = textureArrayResult.albedoArray;
    m_normalTextureArray = textureArrayResult.normalArray;
    m_specularTextureArray = textureArrayResult.specularArray;
    m_textureArrayLayers = std::move(textureArrayResult.layers);
    m_arrayLayerToAtlasTile = std::move(textureArrayResult.layerToAtlasTile);
    m_hasNormalMaps = textureArrayResult.hasNormalMaps;
    m_hasSpecularMaps = textureArrayResult.hasSpecularMaps;

    m_sampler.refreshAnisotropySupport();
    applySamplerToTextureArrays();
}

const TextureAtlas& BlockTextureLibrary::atlas() const {
    return m_atlas;
}

const TextureArray& BlockTextureLibrary::textureArray() const {
    return m_textureArray;
}

const TextureArray& BlockTextureLibrary::normalTextureArray() const {
    return m_normalTextureArray;
}

const TextureArray& BlockTextureLibrary::specularTextureArray() const {
    return m_specularTextureArray;
}

bool BlockTextureLibrary::hasNormalMaps() const {
    return m_hasNormalMaps;
}

bool BlockTextureLibrary::hasSpecularMaps() const {
    return m_hasSpecularMaps;
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

    applySamplerToTextureArrays();
}

float BlockTextureLibrary::anisotropy() const {
    return m_sampler.anisotropy();
}

float BlockTextureLibrary::maxAnisotropy() const {
    return m_sampler.maxAnisotropy();
}
