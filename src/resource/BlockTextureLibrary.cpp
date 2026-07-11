#include "BlockTextureLibrary.h"

#include "BlockTextureArrayBuilder.h"
#include "RhiTextureResourceUtils.h"
#include "TextureAtlasBuilders.h"
#include "../Diagnostics.h"
#include "renderer/rhi/gl/GlRhiTextureRegistry.h"
#include "renderer/rhi/RhiDevice.h"

#include <glad/glad.h>

#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <utility>

namespace {

[[noreturn]] void failBlockTextureLibrary(const std::string& message) {
    std::cerr << message << '\n';
    std::abort();
}

int validateBlockTextureTileSize(const int configuredTileSize) {
    if (configuredTileSize <= 0) {
        failBlockTextureLibrary("Block texture tile size must be positive");
    }
    return configuredTileSize;
}

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

void BlockTextureLibrary::deleteTextureArray(TextureArray& textureArray) {
    if (m_rhiDevice != nullptr && textureArray.texture.isValid()) {
        m_rhiDevice->destroyTexture(textureArray.texture);
    }
    textureArray.texture = {};
    textureArray.tileSize = 16;
    textureArray.layerCount = 0;
}

void BlockTextureLibrary::init(RhiDevice& rhiDevice) {
    assert(m_rhiDevice == nullptr);
    m_rhiDevice = &rhiDevice;
}

void BlockTextureLibrary::shutdown() {
    deleteTextureAtlas(m_atlas);

    deleteTextureArray(m_textureArray);
    deleteTextureArray(m_normalTextureArray);
    deleteTextureArray(m_specularTextureArray);

    m_atlasPixels.clear();
    m_textureArrayLayers.clear();
    m_textureAverageColors.clear();
    m_catalog.clear();
    m_manifest.clear();
    m_arrayLayerToAtlasTile.clear();
    m_hasNormalMaps = false;
    m_hasSpecularMaps = false;
    m_rhiDevice = nullptr;
}

bool BlockTextureLibrary::loadCatalog(const std::string& textureConfigPath) {
    return m_catalog.load(textureConfigPath);
}

bool BlockTextureLibrary::loadCatalog(const std::string& textureConfigPath,
                                      const std::string& packConfigPath) {
    if (!m_catalog.load(textureConfigPath)) {
        return false;
    }
    return m_catalog.loadPackConfig(packConfigPath);
}

void BlockTextureLibrary::buildTextures(const std::string& directory, const int tileSize) {
    buildTextures(directory, tileSize, {});
}

void BlockTextureLibrary::buildTextures(const std::string& directory,
                                        const int tileSize,
                                        const std::unordered_set<std::string>& registeredTextureNames) {
    deleteTextureAtlas(m_atlas);
    deleteTextureArray(m_textureArray);
    deleteTextureArray(m_normalTextureArray);
    deleteTextureArray(m_specularTextureArray);

    m_manifest = registeredTextureNames.empty()
        ? resource::buildBlockTextureManifest(directory)
        : resource::buildBlockTextureManifest(directory, registeredTextureNames);

    const int resolvedTileSize = validateBlockTextureTileSize(tileSize);

    resource::IndexedTextureAtlas atlasResult = resource::buildBlockTextureAtlas(m_manifest, resolvedTileSize, m_catalog);
    m_atlas = atlasResult.atlas;
    m_atlasPixels = std::move(atlasResult.pixels);

    assert(m_rhiDevice != nullptr);
    resource::BlockTextureArraySet textureArrayResult = resource::buildBlockTextureArraySet(
        m_manifest, resolvedTileSize, m_catalog, *m_rhiDevice);
    m_textureArray = textureArrayResult.albedoArray;
    m_normalTextureArray = textureArrayResult.normalArray;
    m_specularTextureArray = textureArrayResult.specularArray;
    m_textureArrayLayers = std::move(textureArrayResult.layers);
    m_arrayLayerToAtlasTile = std::move(textureArrayResult.layerToAtlasTile);
    m_textureAverageColors = std::move(textureArrayResult.layerAverageColors);
    m_hasNormalMaps = textureArrayResult.hasNormalMaps;
    m_hasSpecularMaps = textureArrayResult.hasSpecularMaps;

    m_sampler.refreshAnisotropySupport();
    m_sampler.applyToTexture2D(textureId(m_atlas));
}

void BlockTextureLibrary::buildAtlas(const std::string& directory, const int tileSize) {
    deleteTextureAtlas(m_atlas);

    m_manifest = resource::buildBlockTextureManifest(directory);
    const int resolvedTileSize = validateBlockTextureTileSize(tileSize);
    resource::IndexedTextureAtlas atlasResult = resource::buildBlockTextureAtlas(m_manifest, resolvedTileSize, m_catalog);
    m_atlas = atlasResult.atlas;
    m_atlasPixels = std::move(atlasResult.pixels);

    m_sampler.refreshAnisotropySupport();
    m_sampler.applyToTexture2D(textureId(m_atlas));
}

void BlockTextureLibrary::buildTextureArray(const std::string& directory, const int tileSize) {
    deleteTextureArray(m_textureArray);
    deleteTextureArray(m_normalTextureArray);
    deleteTextureArray(m_specularTextureArray);

    m_manifest = resource::buildBlockTextureManifest(directory);
    const int resolvedTileSize = validateBlockTextureTileSize(tileSize);
    assert(m_rhiDevice != nullptr);
    resource::BlockTextureArraySet textureArrayResult = resource::buildBlockTextureArraySet(
        m_manifest, resolvedTileSize, m_catalog, *m_rhiDevice);
    m_textureArray = textureArrayResult.albedoArray;
    m_normalTextureArray = textureArrayResult.normalArray;
    m_specularTextureArray = textureArrayResult.specularArray;
    m_textureArrayLayers = std::move(textureArrayResult.layers);
    m_arrayLayerToAtlasTile = std::move(textureArrayResult.layerToAtlasTile);
    m_textureAverageColors = std::move(textureArrayResult.layerAverageColors);
    m_hasNormalMaps = textureArrayResult.hasNormalMaps;
    m_hasSpecularMaps = textureArrayResult.hasSpecularMaps;

    m_sampler.refreshAnisotropySupport();
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

int BlockTextureLibrary::tileSize() const {
    return m_catalog.tileSize();
}

const glm::vec3& BlockTextureLibrary::textureAverageColor(const int arrayLayer) const {
    assert(arrayLayer >= 0);
    assert(static_cast<size_t>(arrayLayer) < m_textureAverageColors.size());
    return m_textureAverageColors[static_cast<size_t>(arrayLayer)];
}

int BlockTextureLibrary::textureArrayLayer(const std::string& name) const {
    const auto it = m_textureArrayLayers.find(name);
    if (it != m_textureArrayLayers.end()) {
        return it->second;
    }
    MECRAFT_LOG_FPRINTF(stderr, "[Resource] Unknown block texture array layer: %s\n", name.c_str());
    return -1;
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

    const GLuint atlasTexture = textureId(m_atlas);
    if (atlasTexture != 0) {
        m_sampler.applyToTexture2D(atlasTexture);
    }

}

float BlockTextureLibrary::anisotropy() const {
    return m_sampler.anisotropy();
}

float BlockTextureLibrary::maxAnisotropy() const {
    return m_sampler.maxAnisotropy();
}
