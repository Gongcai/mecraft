#include "BlockTextureArrayBuilder.h"

#include "../third_party/stb/stb_image.h"

#include <algorithm>
#include <array>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

constexpr std::array<unsigned char, 4> kNeutralNormalPixel = {128, 128, 255, 0};
constexpr std::array<unsigned char, 4> kNeutralSpecularPixel = {0, 0, 0, 255};

class PendingTextureArray {
public:
    PendingTextureArray(const int tileSize, const int layerCount) {
        glGenTextures(1, &m_texture.textureID);
        glBindTexture(GL_TEXTURE_2D_ARRAY, m_texture.textureID);
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8,
                     tileSize, tileSize, layerCount,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);

        m_texture.tileSize = tileSize;
        m_texture.layerCount = layerCount;
    }

    PendingTextureArray(const PendingTextureArray&) = delete;
    PendingTextureArray& operator=(const PendingTextureArray&) = delete;

    PendingTextureArray(PendingTextureArray&& other) noexcept
        : m_texture(other.m_texture) {
        other.m_texture = {};
    }

    PendingTextureArray& operator=(PendingTextureArray&& other) noexcept {
        if (this != &other) {
            reset();
            m_texture = other.m_texture;
            other.m_texture = {};
        }
        return *this;
    }

    ~PendingTextureArray() {
        reset();
    }

    [[nodiscard]] GLuint id() const {
        return m_texture.textureID;
    }

    [[nodiscard]] TextureArray release() {
        TextureArray released = m_texture;
        m_texture = {};
        return released;
    }

private:
    void reset() {
        if (m_texture.textureID != 0) {
            glDeleteTextures(1, &m_texture.textureID);
            m_texture.textureID = 0;
        }
    }

    TextureArray m_texture;
};

struct LoadedImage {
    explicit LoadedImage(const std::filesystem::path& path) {
        data = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
        if (data == nullptr || width <= 0 || height <= 0) {
            reset();
            throw std::runtime_error("Failed to load block texture array source: " + path.string());
        }
    }

    LoadedImage(const LoadedImage&) = delete;
    LoadedImage& operator=(const LoadedImage&) = delete;

    ~LoadedImage() {
        reset();
    }

    void reset() {
        if (data != nullptr) {
            stbi_image_free(data);
            data = nullptr;
        }
    }

    unsigned char* data = nullptr;
    int width = 0;
    int height = 0;
    int channels = 0;
};

int computeTextureArrayLayerCount(const resource::BlockTextureManifestEntry& entry,
                                  const BlockTextureCatalog& catalog) {
    const BlockTextureCatalogEntry* catalogEntry = catalog.find(entry.name);
    const bool metadataAnimated = entry.animationMetadata.has_value();
    const bool catalogAnimated =
        catalogEntry != nullptr &&
        catalogEntry->verticalFrames &&
        catalogEntry->animation.frameCount > 1;
    if (!metadataAnimated && !catalogAnimated) {
        return 1;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    if (!stbi_info(entry.albedoPath.string().c_str(), &width, &height, &channels)) {
        throw std::runtime_error("Failed to inspect block texture: " + entry.albedoPath.string());
    }
    if (width <= 0 || height <= 0) {
        throw std::runtime_error("Invalid block animated texture dimensions: " + entry.albedoPath.string());
    }

    if (!metadataAnimated && height == width) {
        return 1;
    }

    if (metadataAnimated) {
        if (height % width != 0) {
            throw std::runtime_error("Texture metadata source height must be a multiple of width for " +
                                     entry.albedoPath.string());
        }
        const int physicalFrameCount = height / width;
        if (entry.animationMetadata->maxExplicitFrameIndex >= physicalFrameCount) {
            throw std::runtime_error("Texture metadata frame index exceeds vertical frames for " + entry.albedoPath.string());
        }
        return physicalFrameCount;
    }

    const TextureAnimationInfo& animation = catalogEntry->animation;
    if (height != width * animation.frameCount) {
        throw std::runtime_error("Texture catalog dimensions do not match vertical frames for " + entry.albedoPath.string());
    }

    return catalogEntry->animation.frameCount;
}

void uploadTextureArrayLayer(const GLuint textureId,
                             const int targetLayer,
                             const unsigned char* srcPixels,
                             const int tileSize,
                             const bool zeroAlpha) {
    glBindTexture(GL_TEXTURE_2D_ARRAY, textureId);
    if (zeroAlpha) {
        std::vector<unsigned char> pixels(static_cast<size_t>(tileSize) * static_cast<size_t>(tileSize) * 4);
        for (size_t index = 0; index < pixels.size(); index += 4) {
            pixels[index + 0] = srcPixels[index + 0];
            pixels[index + 1] = srcPixels[index + 1];
            pixels[index + 2] = srcPixels[index + 2];
            pixels[index + 3] = 0;
        }
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, targetLayer,
                        tileSize, tileSize, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        return;
    }
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, targetLayer,
                    tileSize, tileSize, 1, GL_RGBA, GL_UNSIGNED_BYTE, srcPixels);
}

void uploadSourceTileToArrayLayer(const GLuint textureId,
                                  const int targetLayer,
                                  const unsigned char* srcPixels,
                                  const int sourceTileSize,
                                  const int sourceRowWidth,
                                  const int targetTileSize,
                                  const bool zeroAlpha) {
    if (sourceTileSize <= 0 || sourceRowWidth <= 0 || targetTileSize <= 0) {
        throw std::runtime_error("Block texture array tile sizes must be positive");
    }

    if (sourceTileSize == targetTileSize && sourceRowWidth == targetTileSize) {
        uploadTextureArrayLayer(textureId, targetLayer, srcPixels, targetTileSize, zeroAlpha);
        return;
    }

    std::vector<unsigned char> pixels(static_cast<size_t>(targetTileSize) * static_cast<size_t>(targetTileSize) * 4);
    for (int y = 0; y < targetTileSize; ++y) {
        const int sourceY = y * sourceTileSize / targetTileSize;
        for (int x = 0; x < targetTileSize; ++x) {
            const int sourceX = x * sourceTileSize / targetTileSize;
            const int srcIndex = (sourceY * sourceRowWidth + sourceX) * 4;
            const int dstIndex = (y * targetTileSize + x) * 4;
            pixels[dstIndex + 0] = srcPixels[srcIndex + 0];
            pixels[dstIndex + 1] = srcPixels[srcIndex + 1];
            pixels[dstIndex + 2] = srcPixels[srcIndex + 2];
            pixels[dstIndex + 3] = zeroAlpha ? 0 : srcPixels[srcIndex + 3];
        }
    }

    glBindTexture(GL_TEXTURE_2D_ARRAY, textureId);
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, targetLayer,
                    targetTileSize, targetTileSize, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
}

void uploadConstantLayer(const GLuint textureId,
                         const int targetLayer,
                         const int tileSize,
                         const std::array<unsigned char, 4>& pixel) {
    std::vector<unsigned char> pixels(static_cast<size_t>(tileSize) * static_cast<size_t>(tileSize) * 4);
    for (size_t i = 0; i < pixels.size(); i += 4) {
        pixels[i + 0] = pixel[0];
        pixels[i + 1] = pixel[1];
        pixels[i + 2] = pixel[2];
        pixels[i + 3] = pixel[3];
    }
    uploadTextureArrayLayer(textureId, targetLayer, pixels.data(), tileSize, false);
}

void uploadNeutralMaterialLayers(const GLuint textureId,
                                 const int firstLayer,
                                 const int layerCount,
                                 const int tileSize,
                                 const std::array<unsigned char, 4>& pixel) {
    for (int layer = 0; layer < layerCount; ++layer) {
        uploadConstantLayer(textureId, firstLayer + layer, tileSize, pixel);
    }
}

void uploadAlbedoLayers(const GLuint textureId,
                        const resource::BlockTextureManifestEntry& entry,
                        const int firstLayer,
                        const int layerCount,
                        const int tileSize,
                        const BlockTextureCatalogEntry* catalogEntry) {
    LoadedImage image(entry.albedoPath);
    const int sourceTileSize = image.width;

    if (layerCount > 1) {
        if (image.height != sourceTileSize * layerCount) {
            throw std::runtime_error("Texture catalog dimensions do not match texture array source for " + entry.albedoPath.string());
        }

        const bool topFrameFirst = catalogEntry != nullptr && catalogEntry->topFrameFirst;
        for (int frame = 0; frame < layerCount; ++frame) {
            const int sourceFrameIndex = topFrameFirst ? layerCount - 1 - frame : frame;
            const unsigned char* framePixels = image.data + static_cast<size_t>(sourceFrameIndex * sourceTileSize * image.width) * 4;
            uploadSourceTileToArrayLayer(textureId, firstLayer + frame, framePixels,
                                         sourceTileSize, image.width, tileSize, false);
        }
        return;
    }

    if (image.width != image.height) {
        throw std::runtime_error("Block texture array source dimensions must be square for " + entry.albedoPath.string());
    }
    uploadSourceTileToArrayLayer(textureId, firstLayer, image.data, sourceTileSize, image.width, tileSize, false);
}

void uploadMaterialMapLayers(const GLuint textureId,
                             const std::optional<std::filesystem::path>& mapPath,
                             const int firstLayer,
                             const int layerCount,
                             const int tileSize,
                             const bool topFrameFirst,
                             const std::array<unsigned char, 4>& neutralPixel,
                             const bool zeroAlphaWhenSourceHasNoAlpha,
                             const char* roleName) {
    if (!mapPath.has_value()) {
        uploadNeutralMaterialLayers(textureId, firstLayer, layerCount, tileSize, neutralPixel);
        return;
    }

    LoadedImage image(mapPath.value());
    const int sourceTileSize = image.width;
    const bool zeroAlpha = zeroAlphaWhenSourceHasNoAlpha && image.channels < 4;
    if (image.width == image.height) {
        for (int layer = 0; layer < layerCount; ++layer) {
            uploadSourceTileToArrayLayer(textureId, firstLayer + layer, image.data,
                                         sourceTileSize, image.width, tileSize, zeroAlpha);
        }
        return;
    }

    if (layerCount > 1 && image.height % sourceTileSize == 0) {
        const int sourceFrameCount = image.height / sourceTileSize;
        if (sourceFrameCount <= 0) {
            throw std::runtime_error(std::string("Block ") + roleName +
                                     " texture frame count must be positive for " +
                                     mapPath.value().string());
        }
        for (int frame = 0; frame < layerCount; ++frame) {
            const int frameInMaterialCycle = frame % sourceFrameCount;
            const int sourceFrameIndex = topFrameFirst
                ? sourceFrameCount - 1 - frameInMaterialCycle
                : frameInMaterialCycle;
            const unsigned char* framePixels = image.data + static_cast<size_t>(sourceFrameIndex * sourceTileSize * image.width) * 4;
            uploadSourceTileToArrayLayer(textureId, firstLayer + frame, framePixels,
                                         sourceTileSize, image.width, tileSize, zeroAlpha);
        }
        return;
    }

    throw std::runtime_error(std::string("Block ") + roleName +
                             " texture dimensions do not match layer layout for " +
                             mapPath.value().string());
}

void finalizeTextureArray(const GLuint textureId) {
    glBindTexture(GL_TEXTURE_2D_ARRAY, textureId);
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

} // namespace

namespace resource {

BlockTextureArraySet buildBlockTextureArraySet(const std::string& directory,
                                               const int tileSize,
                                               BlockTextureCatalog& catalog) {
    return buildBlockTextureArraySet(buildBlockTextureManifest(directory), tileSize, catalog);
}

BlockTextureArraySet buildBlockTextureArraySet(const BlockTextureManifest& manifest,
                                               const int tileSize,
                                               BlockTextureCatalog& catalog) {
    if (tileSize <= 0) {
        return buildBlockTextureArraySet(manifest, inferBlockTextureTileSizes(manifest, catalog), catalog);
    }

    BlockTextureTileSizes tileSizes;
    tileSizes.albedo = tileSize;
    tileSizes.normal = tileSize;
    tileSizes.specular = tileSize;
    return buildBlockTextureArraySet(manifest, tileSizes, catalog);
}

BlockTextureArraySet buildBlockTextureArraySet(const BlockTextureManifest& manifest,
                                               const BlockTextureTileSizes& tileSizes,
                                               BlockTextureCatalog& catalog) {
    if (tileSizes.albedo <= 0) {
        throw std::runtime_error("Block texture array albedo tile size must be positive");
    }

    const std::vector<BlockTextureManifestEntry>& textureEntries = manifest.entries();
    if (textureEntries.empty()) {
        throw std::runtime_error("Block texture array manifest contains no albedo textures");
    }

    for (auto& [_, texture] : catalog.entries()) {
        texture.animation.firstLayer = 0;
        if (texture.animation.frameCount > 1) {
            texture.animation.isAnimated = false;
        }
    }

    std::vector<int> layersPerImage;
    layersPerImage.reserve(textureEntries.size());

    int numLayers = 0;
    for (const BlockTextureManifestEntry& entry : textureEntries) {
        const int layerCount = computeTextureArrayLayerCount(entry, catalog);
        layersPerImage.push_back(layerCount);
        numLayers += layerCount;
    }

    if (numLayers > 65535) {
        throw std::runtime_error("Block texture array exceeds the uint16 vertex layer encoding limit");
    }

    GLint maxTextureArrayLayers = 0;
    glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &maxTextureArrayLayers);
    if (maxTextureArrayLayers <= 0) {
        throw std::runtime_error("Failed to query GL_MAX_ARRAY_TEXTURE_LAYERS");
    }
    if (numLayers > maxTextureArrayLayers) {
        throw std::runtime_error("Block texture array exceeds GL_MAX_ARRAY_TEXTURE_LAYERS");
    }

    PendingTextureArray albedoArray(tileSizes.albedo, numLayers);
    std::optional<PendingTextureArray> normalArray;
    std::optional<PendingTextureArray> specularArray;
    if (manifest.hasNormalMaps()) {
        if (tileSizes.normal <= 0) {
            throw std::runtime_error("Block normal texture array tile size must be positive");
        }
        normalArray.emplace(tileSizes.normal, numLayers);
    }
    if (manifest.hasSpecularMaps()) {
        if (tileSizes.specular <= 0) {
            throw std::runtime_error("Block specular texture array tile size must be positive");
        }
        specularArray.emplace(tileSizes.specular, numLayers);
    }

    stbi_set_flip_vertically_on_load(true);

    BlockTextureArraySet result;
    result.hasNormalMaps = manifest.hasNormalMaps();
    result.hasSpecularMaps = manifest.hasSpecularMaps();

    int currentLayer = 0;
    for (size_t imageIndex = 0; imageIndex < textureEntries.size(); ++imageIndex) {
        const BlockTextureManifestEntry& entry = textureEntries[imageIndex];
        const int layerCount = layersPerImage[imageIndex];

        result.layers[entry.name] = currentLayer;

        if (entry.animationMetadata.has_value() && layerCount > 1) {
            BlockTextureCatalogEntry& metadataEntry = catalog.entries()[entry.name];
            metadataEntry.animation.frameCount = layerCount;
            metadataEntry.animation.fps = 20.0f / static_cast<float>(entry.animationMetadata->frameTimeTicks);
            metadataEntry.animation.isAnimated = true;
            metadataEntry.verticalFrames = true;
            metadataEntry.topFrameFirst = true;
        }

        BlockTextureCatalogEntry* catalogEntry = catalog.findMutable(entry.name);
        const bool useAnimationFrames = layerCount > 1;
        if (catalogEntry != nullptr) {
            catalogEntry->animation.firstLayer = currentLayer;
            catalogEntry->animation.isAnimated = useAnimationFrames && catalogEntry->animation.frameCount > 1;
        }

        const bool topFrameFirst = catalogEntry != nullptr && catalogEntry->topFrameFirst;
        uploadAlbedoLayers(albedoArray.id(), entry, currentLayer, layerCount, tileSizes.albedo, catalogEntry);
        if (normalArray.has_value()) {
            uploadMaterialMapLayers(normalArray->id(), entry.normalPath, currentLayer, layerCount, tileSizes.normal,
                                    topFrameFirst, kNeutralNormalPixel, true, "normal");
        }
        if (specularArray.has_value()) {
            uploadMaterialMapLayers(specularArray->id(), entry.specularPath, currentLayer, layerCount, tileSizes.specular,
                                    topFrameFirst, kNeutralSpecularPixel, false, "specular");
        }

        currentLayer += layerCount;
    }

    finalizeTextureArray(albedoArray.id());
    if (normalArray.has_value()) {
        finalizeTextureArray(normalArray->id());
    }
    if (specularArray.has_value()) {
        finalizeTextureArray(specularArray->id());
    }

    int mapLayer = 0;
    for (size_t mapIdx = 0; mapIdx < textureEntries.size(); ++mapIdx) {
        const int atlasTile = static_cast<int>(mapIdx);
        const int layerCount = layersPerImage[mapIdx];
        for (int frame = 0; frame < layerCount; ++frame) {
            result.layerToAtlasTile[mapLayer + frame] = atlasTile;
        }
        mapLayer += layerCount;
    }

    result.albedoArray = albedoArray.release();
    if (normalArray.has_value()) {
        result.normalArray = normalArray->release();
    }
    if (specularArray.has_value()) {
        result.specularArray = specularArray->release();
    }
    return result;
}

} // namespace resource
