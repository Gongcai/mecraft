#include "BlockTextureArrayBuilder.h"
#include "TextureResampler.h"

#include "../third_party/stb/stb_image.h"

#include <glm/vec3.hpp>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

[[noreturn]] void failBlockTextureArrayBuilder(const std::string& message) {
    std::cerr << message << '\n';
    std::abort();
}

constexpr std::array<unsigned char, 4> kNeutralNormalPixel = {128, 128, 255, 0};
constexpr std::array<unsigned char, 4> kNeutralSpecularPixel = {0, 0, 0, 0};

struct ResourceTint {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
};

ResourceTint textureTintColor(const ResourceTextureTint tint) {
    switch (tint) {
        case ResourceTextureTint::Grass:
        case ResourceTextureTint::Foliage:
            return {0.50f, 0.78f, 0.34f};
        case ResourceTextureTint::None:
        default:
            return {};
    }
}

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
            failBlockTextureArrayBuilder("Failed to load block texture array source: " + path.string());
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
                                  const int tileSize,
                                  const BlockTextureCatalog& catalog) {
    if (tileSize <= 0) {
        failBlockTextureArrayBuilder("Block texture array tile size must be positive");
    }

    const BlockTextureCatalogEntry* catalogEntry = catalog.find(entry.name);
    if (catalogEntry == nullptr ||
        !catalogEntry->verticalFrames ||
        catalogEntry->animation.frameCount <= 1) {
        return 1;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    if (!stbi_info(entry.albedoPath.string().c_str(), &width, &height, &channels)) {
        failBlockTextureArrayBuilder("Failed to inspect block texture: " + entry.albedoPath.string());
    }
    if (width <= 0 || height <= 0) {
        failBlockTextureArrayBuilder("Block texture dimensions must be positive for " + entry.albedoPath.string());
    }

    const TextureAnimationInfo& animation = catalogEntry->animation;
    if (height != width * animation.frameCount) {
        failBlockTextureArrayBuilder("Texture catalog dimensions do not match vertical frames for " + entry.albedoPath.string());
    }

    return animation.frameCount;
}

void uploadTextureArrayLayer(const GLuint textureId,
                             const int targetLayer,
                             const unsigned char* srcPixels,
                             const int tileSize) {
    glBindTexture(GL_TEXTURE_2D_ARRAY, textureId);
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, targetLayer,
                    tileSize, tileSize, 1, GL_RGBA, GL_UNSIGNED_BYTE, srcPixels);
}

glm::vec3 computeAlphaWeightedAverageColor(const unsigned char* srcPixels,
                                           const int rowWidth,
                                           const int tileSize,
                                           const ResourceTint tint) {
    double red = 0.0;
    double green = 0.0;
    double blue = 0.0;
    double alpha = 0.0;

    for (int y = 0; y < tileSize; ++y) {
        for (int x = 0; x < tileSize; ++x) {
            const size_t pixelIndex = static_cast<size_t>(y * rowWidth + x) * 4u;
            const double pixelAlpha = static_cast<double>(srcPixels[pixelIndex + 3]) * (1.0 / 255.0);
            red += static_cast<double>(srcPixels[pixelIndex + 0]) * (1.0 / 255.0) *
                   static_cast<double>(tint.r) * pixelAlpha;
            green += static_cast<double>(srcPixels[pixelIndex + 1]) * (1.0 / 255.0) *
                     static_cast<double>(tint.g) * pixelAlpha;
            blue += static_cast<double>(srcPixels[pixelIndex + 2]) * (1.0 / 255.0) *
                    static_cast<double>(tint.b) * pixelAlpha;
            alpha += pixelAlpha;
        }
    }

    if (alpha <= 0.0) {
        return glm::vec3(0.0f);
    }

    const float invAlpha = static_cast<float>(1.0 / alpha);
    return glm::vec3(static_cast<float>(red) * invAlpha,
                     static_cast<float>(green) * invAlpha,
                     static_cast<float>(blue) * invAlpha);
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
    uploadTextureArrayLayer(textureId, targetLayer, pixels.data(), tileSize);
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

std::vector<unsigned char> makeTextureArrayTilePixels(const unsigned char* sourcePixels,
                                                      const int sourceWidth,
                                                      const int sourceHeight,
                                                      const int sourceRowStridePixels,
                                                      const int tileSize) {
    const resource::TextureResampleFilter filter =
        resource::selectTextureTileResampleFilter(sourceWidth, sourceHeight, tileSize);
    return resource::resampleRgba8(sourcePixels,
                                   sourceWidth,
                                   sourceHeight,
                                   sourceRowStridePixels,
                                   tileSize,
                                   tileSize,
                                   filter);
}

void uploadAlbedoLayers(const GLuint textureId,
                        const resource::BlockTextureManifestEntry& entry,
                        const int firstLayer,
                        const int layerCount,
                        const int tileSize,
                        const BlockTextureCatalogEntry* catalogEntry,
                        std::vector<glm::vec3>& layerAverageColors) {
    LoadedImage image(entry.albedoPath);
    const ResourceTint tint = textureTintColor(catalogEntry != nullptr
        ? catalogEntry->tint
        : ResourceTextureTint::None);

    if (layerCount > 1) {
        if (image.height != image.width * layerCount) {
            failBlockTextureArrayBuilder("Texture catalog dimensions do not match texture array source for " + entry.albedoPath.string());
        }

        const bool topFrameFirst = catalogEntry != nullptr && catalogEntry->topFrameFirst;
        for (int frame = 0; frame < layerCount; ++frame) {
            const int sourceFrameIndex = topFrameFirst ? layerCount - 1 - frame : frame;
            const unsigned char* framePixels =
                image.data + static_cast<size_t>(sourceFrameIndex * image.width * image.width) * 4U;
            const std::vector<unsigned char> tilePixels =
                makeTextureArrayTilePixels(framePixels, image.width, image.width, image.width, tileSize);
            uploadTextureArrayLayer(textureId, firstLayer + frame, tilePixels.data(), tileSize);
            layerAverageColors[static_cast<size_t>(firstLayer + frame)] =
                computeAlphaWeightedAverageColor(tilePixels.data(), tileSize, tileSize, tint);
        }
        return;
    }

    if (image.height != image.width) {
        failBlockTextureArrayBuilder("Block texture array source must be square for " + entry.albedoPath.string());
    }
    const std::vector<unsigned char> tilePixels =
        makeTextureArrayTilePixels(image.data, image.width, image.height, image.width, tileSize);
    uploadTextureArrayLayer(textureId, firstLayer, tilePixels.data(), tileSize);
    layerAverageColors[static_cast<size_t>(firstLayer)] =
        computeAlphaWeightedAverageColor(tilePixels.data(), tileSize, tileSize, tint);
}

void uploadMaterialMapLayers(const GLuint textureId,
                             const std::optional<std::filesystem::path>& mapPath,
                             const int firstLayer,
                             const int layerCount,
                             const int tileSize,
                             const bool topFrameFirst,
                             const std::array<unsigned char, 4>& neutralPixel,
                             const char* roleName) {
    if (!mapPath.has_value()) {
        uploadNeutralMaterialLayers(textureId, firstLayer, layerCount, tileSize, neutralPixel);
        return;
    }

    LoadedImage image(mapPath.value());
    if (image.height == image.width) {
        const std::vector<unsigned char> tilePixels =
            makeTextureArrayTilePixels(image.data, image.width, image.height, image.width, tileSize);
        for (int layer = 0; layer < layerCount; ++layer) {
            uploadTextureArrayLayer(textureId, firstLayer + layer, tilePixels.data(), tileSize);
        }
        return;
    }

    if (image.height == image.width * layerCount) {
        for (int frame = 0; frame < layerCount; ++frame) {
            const int sourceFrameIndex = topFrameFirst ? layerCount - 1 - frame : frame;
            const unsigned char* framePixels =
                image.data + static_cast<size_t>(sourceFrameIndex * image.width * image.width) * 4U;
            const std::vector<unsigned char> tilePixels =
                makeTextureArrayTilePixels(framePixels, image.width, image.width, image.width, tileSize);
            uploadTextureArrayLayer(textureId, firstLayer + frame, tilePixels.data(), tileSize);
        }
        return;
    }

    failBlockTextureArrayBuilder(std::string("Block ") + roleName +
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
        failBlockTextureArrayBuilder("Block texture array tile size must be positive");
    }

    const std::vector<BlockTextureManifestEntry>& textureEntries = manifest.entries();
    if (textureEntries.empty()) {
        failBlockTextureArrayBuilder("Block texture array manifest contains no albedo textures");
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
        const int layerCount = computeTextureArrayLayerCount(entry, tileSize, catalog);
        layersPerImage.push_back(layerCount);
        numLayers += layerCount;
    }

    if (numLayers > 1024) {
        failBlockTextureArrayBuilder("Block texture array exceeds the 1024-layer vertex encoding limit");
    }

    PendingTextureArray albedoArray(tileSize, numLayers);
    std::optional<PendingTextureArray> normalArray;
    std::optional<PendingTextureArray> specularArray;
    if (manifest.hasNormalMaps()) {
        normalArray.emplace(tileSize, numLayers);
    }
    if (manifest.hasSpecularMaps()) {
        specularArray.emplace(tileSize, numLayers);
    }

    stbi_set_flip_vertically_on_load(true);

    BlockTextureArraySet result;
    result.hasNormalMaps = manifest.hasNormalMaps();
    result.hasSpecularMaps = manifest.hasSpecularMaps();
    result.layerAverageColors.assign(static_cast<size_t>(numLayers), glm::vec3(0.0f));

    int currentLayer = 0;
    for (size_t imageIndex = 0; imageIndex < textureEntries.size(); ++imageIndex) {
        const BlockTextureManifestEntry& entry = textureEntries[imageIndex];
        const int layerCount = layersPerImage[imageIndex];

        result.layers[entry.name] = currentLayer;

        BlockTextureCatalogEntry* catalogEntry = catalog.findMutable(entry.name);
        const bool useAnimationFrames = layerCount > 1;
        if (catalogEntry != nullptr) {
            catalogEntry->animation.firstLayer = currentLayer;
            catalogEntry->animation.isAnimated = useAnimationFrames && catalogEntry->animation.frameCount > 1;
        }

        const bool topFrameFirst = catalogEntry != nullptr && catalogEntry->topFrameFirst;
        uploadAlbedoLayers(albedoArray.id(), entry, currentLayer, layerCount, tileSize, catalogEntry,
                           result.layerAverageColors);
        if (normalArray.has_value()) {
            uploadMaterialMapLayers(normalArray->id(), entry.normalPath, currentLayer, layerCount, tileSize,
                                    topFrameFirst, kNeutralNormalPixel, "normal");
        }
        if (specularArray.has_value()) {
            uploadMaterialMapLayers(specularArray->id(), entry.specularPath, currentLayer, layerCount, tileSize,
                                    topFrameFirst, kNeutralSpecularPixel, "specular");
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
