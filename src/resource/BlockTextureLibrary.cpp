#include "BlockTextureLibrary.h"

#include "BlockTextureArrayBuilder.h"
#include "TextureAtlasBuilders.h"
#include "../Diagnostics.h"
#include "../third_party/stb/stb_image.h"

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

int textureFrameCountForTileSize(const BlockTextureCatalog& catalog,
                                 const resource::BlockTextureManifestEntry& entry) {
    const BlockTextureCatalogEntry* catalogEntry = catalog.find(entry.name);
    if (catalogEntry == nullptr ||
        !catalogEntry->verticalFrames ||
        catalogEntry->animation.frameCount <= 1) {
        return 1;
    }
    return catalogEntry->animation.frameCount;
}

int resolveBlockTextureTileSize(const resource::BlockTextureManifest& manifest,
                                const int configuredTileSize,
                                const BlockTextureCatalog& catalog) {
    if (configuredTileSize <= 0) {
        failBlockTextureLibrary("Block texture tile size must be positive");
    }

    int resolvedTileSize = 0;
    for (const resource::BlockTextureManifestEntry& entry : manifest.entries()) {
        int width = 0;
        int height = 0;
        int channels = 0;
        if (!stbi_info(entry.albedoPath.string().c_str(), &width, &height, &channels)) {
            failBlockTextureLibrary("Failed to inspect block texture: " + entry.albedoPath.string());
        }
        if (width <= 0 || height <= 0) {
            failBlockTextureLibrary("Block texture dimensions must be positive: " + entry.albedoPath.string());
        }

        const int frameCount = textureFrameCountForTileSize(catalog, entry);
        if (frameCount > 1) {
            if (height != width * frameCount) {
                failBlockTextureLibrary("Animated block texture dimensions do not match declared frames: " +
                                        entry.albedoPath.string());
            }
        } else if (height != width) {
            failBlockTextureLibrary("Block texture must be square: " + entry.albedoPath.string());
        }

        if (resolvedTileSize == 0) {
            resolvedTileSize = width;
        } else if (resolvedTileSize != width) {
            failBlockTextureLibrary("Block texture directory mixes tile sizes: " + entry.albedoPath.string());
        }
    }

    if (resolvedTileSize == 0) {
        return configuredTileSize;
    }
    if (resolvedTileSize != configuredTileSize) {
        std::cerr << "[Resource] Block texture tile size inferred from registered textures: "
                  << resolvedTileSize << "px (catalog: " << configuredTileSize << "px)\n";
    }
    return resolvedTileSize;
}

} // namespace

void BlockTextureLibrary::deleteTextureArray(TextureArray& textureArray) {
    if (textureArray.textureID != 0) {
        glDeleteTextures(1, &textureArray.textureID);
        textureArray.textureID = 0;
    }
    textureArray.tileSize = 16;
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
    m_textureAverageColors.clear();
    m_catalog.clear();
    m_manifest.clear();
    m_arrayLayerToAtlasTile.clear();
    m_hasNormalMaps = false;
    m_hasSpecularMaps = false;
}

bool BlockTextureLibrary::loadCatalog(const std::string& textureConfigPath) {
    return m_catalog.load(textureConfigPath);
}

void BlockTextureLibrary::buildTextures(const std::string& directory, const int tileSize) {
    buildTextures(directory, tileSize, {});
}

void BlockTextureLibrary::buildTextures(const std::string& directory,
                                        const int tileSize,
                                        const std::unordered_set<std::string>& registeredTextureNames) {
    if (m_atlas.textureID != 0) {
        glDeleteTextures(1, &m_atlas.textureID);
        m_atlas.textureID = 0;
    }
    deleteTextureArray(m_textureArray);
    deleteTextureArray(m_normalTextureArray);
    deleteTextureArray(m_specularTextureArray);

    m_manifest = registeredTextureNames.empty()
        ? resource::buildBlockTextureManifest(directory)
        : resource::buildBlockTextureManifest(directory, registeredTextureNames);

    const int resolvedTileSize = resolveBlockTextureTileSize(m_manifest, tileSize, m_catalog);

    resource::IndexedTextureAtlas atlasResult = resource::buildBlockTextureAtlas(m_manifest, resolvedTileSize, m_catalog);
    m_atlas = atlasResult.atlas;
    m_atlasPixels = std::move(atlasResult.pixels);

    resource::BlockTextureArraySet textureArrayResult = resource::buildBlockTextureArraySet(m_manifest, resolvedTileSize, m_catalog);
    m_textureArray = textureArrayResult.albedoArray;
    m_normalTextureArray = textureArrayResult.normalArray;
    m_specularTextureArray = textureArrayResult.specularArray;
    m_textureArrayLayers = std::move(textureArrayResult.layers);
    m_arrayLayerToAtlasTile = std::move(textureArrayResult.layerToAtlasTile);
    m_textureAverageColors = std::move(textureArrayResult.layerAverageColors);
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
    const int resolvedTileSize = resolveBlockTextureTileSize(m_manifest, tileSize, m_catalog);
    resource::IndexedTextureAtlas atlasResult = resource::buildBlockTextureAtlas(m_manifest, resolvedTileSize, m_catalog);
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
    const int resolvedTileSize = resolveBlockTextureTileSize(m_manifest, tileSize, m_catalog);
    resource::BlockTextureArraySet textureArrayResult = resource::buildBlockTextureArraySet(m_manifest, resolvedTileSize, m_catalog);
    m_textureArray = textureArrayResult.albedoArray;
    m_normalTextureArray = textureArrayResult.normalArray;
    m_specularTextureArray = textureArrayResult.specularArray;
    m_textureArrayLayers = std::move(textureArrayResult.layers);
    m_arrayLayerToAtlasTile = std::move(textureArrayResult.layerToAtlasTile);
    m_textureAverageColors = std::move(textureArrayResult.layerAverageColors);
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
