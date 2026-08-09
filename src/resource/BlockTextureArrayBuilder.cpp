#include "BlockTextureArrayBuilder.h"
#include "TextureResampler.h"
#include "renderer/contracts/GpuMaterialContract.h"
#include "renderer/contracts/TerrainMaterialSamplingContract.h"
#include "renderer/rhi/RhiCommandList.h"
#include "renderer/rhi/RhiDevice.h"

#include "../third_party/stb/stb_image.h"

#include <glm/vec3.hpp>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

[[noreturn]] void failBlockTextureArrayBuilder(const std::string& message) {
    std::cerr << message << '\n';
    std::abort();
}

constexpr std::array<unsigned char, 4> kNeutralNormalPixel = {128, 128, 255, 255};
constexpr std::array<unsigned char, 4> kNeutralSpecularPixel = {0, 254, 0, 255};

enum class MaterialMapEncoding { LabPbrNormal, LabPbrSpecular };

struct ResourceTint {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
};

ResourceTint textureTintColor(const ResourceTextureTint tint) {
    switch (tint) {
    case ResourceTextureTint::Grass:
    case ResourceTextureTint::Foliage: return {0.50f, 0.78f, 0.34f};
    case ResourceTextureTint::None:
    default: return {};
    }
}

class PendingTextureArray {
public:
    PendingTextureArray(const int tileSize, const int layerCount)
        : m_tileSize(tileSize), m_layerCount(layerCount),
          m_pixels(static_cast<size_t>(tileSize) * tileSize * layerCount * 4u) {}

    PendingTextureArray(const PendingTextureArray&) = delete;
    PendingTextureArray& operator=(const PendingTextureArray&) = delete;

    void writeLayer(const int layer, const unsigned char* pixels) {
        const size_t layerSize = static_cast<size_t>(m_tileSize) * m_tileSize * 4u;
        std::copy_n(pixels, layerSize, m_pixels.data() + static_cast<size_t>(layer) * layerSize);
    }

    [[nodiscard]] TextureArray create(RhiDevice& rhiDevice, const char* debugName) const {
        RhiTextureDesc desc;
        desc.debugName = debugName;
        desc.dimension = RhiTextureDimension::Texture2DArray;
        desc.format = RhiTextureFormat::Rgba8Unorm;
        desc.width = static_cast<uint32_t>(m_tileSize);
        desc.height = static_cast<uint32_t>(m_tileSize);
        desc.depthOrLayers = static_cast<uint32_t>(m_layerCount);
        desc.mipLevels = 1u + static_cast<uint32_t>(std::floor(std::log2(static_cast<double>(m_tileSize))));
        desc.usage = rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::TransferSrc) |
                     rhiFlag(RhiTextureUsage::TransferDst);
        desc.memoryCategory = RhiMemoryCategory::Texture;
        RhiTextureInitialData initialData;
        initialData.pixels = m_pixels.data();
        initialData.sizeBytes = m_pixels.size();
        initialData.layerCount = static_cast<uint32_t>(m_layerCount);
        initialData.finalState = RhiResourceState::TransferDst;

        TextureArray texture;
        texture.texture = rhiDevice.createTexture(desc, &initialData);
        texture.tileSize = m_tileSize;
        texture.layerCount = m_layerCount;
        if (!texture.texture.isValid()) {
            failBlockTextureArrayBuilder("Failed to create block texture array RHI resource");
        }
        return texture;
    }

    /// Transfers the normalized RGBA8 level-zero pixels after GPU resource creation.
    /// @return Layer-major pixels with four bytes per texel.
    [[nodiscard]] std::vector<unsigned char> releasePixels() && { return std::move(m_pixels); }

private:
    int m_tileSize = 0;
    int m_layerCount = 0;
    std::vector<unsigned char> m_pixels;
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

    ~LoadedImage() { reset(); }

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

int computeTextureArrayLayerCount(const resource::BlockTextureManifestEntry& entry, const int tileSize,
                                  const BlockTextureCatalog& catalog) {
    if (tileSize <= 0) {
        failBlockTextureArrayBuilder("Block texture array tile size must be positive");
    }

    const BlockTextureCatalogEntry* catalogEntry = catalog.find(entry.name);
    if (catalogEntry == nullptr || !catalogEntry->verticalFrames || catalogEntry->animation.frameCount <= 1) {
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
        failBlockTextureArrayBuilder("Texture catalog dimensions do not match vertical frames for " +
                                     entry.albedoPath.string());
    }

    return animation.frameCount;
}

void writeTextureArrayLayer(PendingTextureArray& texture, const int targetLayer, const unsigned char* srcPixels,
                            const int tileSize) {
    (void)tileSize;
    texture.writeLayer(targetLayer, srcPixels);
}

glm::vec3 computeAlphaWeightedAverageColor(const unsigned char* srcPixels, const int rowWidth, const int tileSize,
                                           const ResourceTint tint) {
    double red = 0.0;
    double green = 0.0;
    double blue = 0.0;
    double alpha = 0.0;

    for (int y = 0; y < tileSize; ++y) {
        for (int x = 0; x < tileSize; ++x) {
            const size_t pixelIndex = static_cast<size_t>(y * rowWidth + x) * 4u;
            const double pixelAlpha = static_cast<double>(srcPixels[pixelIndex + 3]) * (1.0 / 255.0);
            red += static_cast<double>(srcPixels[pixelIndex + 0]) * (1.0 / 255.0) * static_cast<double>(tint.r) *
                   pixelAlpha;
            green += static_cast<double>(srcPixels[pixelIndex + 1]) * (1.0 / 255.0) * static_cast<double>(tint.g) *
                     pixelAlpha;
            blue += static_cast<double>(srcPixels[pixelIndex + 2]) * (1.0 / 255.0) * static_cast<double>(tint.b) *
                    pixelAlpha;
            alpha += pixelAlpha;
        }
    }

    if (alpha <= 0.0) {
        return glm::vec3(0.0f);
    }

    const float invAlpha = static_cast<float>(1.0 / alpha);
    return glm::vec3(static_cast<float>(red) * invAlpha, static_cast<float>(green) * invAlpha,
                     static_cast<float>(blue) * invAlpha);
}

void writeConstantLayer(PendingTextureArray& texture, const int targetLayer, const int tileSize,
                        const std::array<unsigned char, 4>& pixel) {
    std::vector<unsigned char> pixels(static_cast<size_t>(tileSize) * static_cast<size_t>(tileSize) * 4);
    for (size_t i = 0; i < pixels.size(); i += 4) {
        pixels[i + 0] = pixel[0];
        pixels[i + 1] = pixel[1];
        pixels[i + 2] = pixel[2];
        pixels[i + 3] = pixel[3];
    }
    writeTextureArrayLayer(texture, targetLayer, pixels.data(), tileSize);
}

void writeNeutralMaterialLayers(PendingTextureArray& texture, const int firstLayer, const int layerCount,
                                const int tileSize, const std::array<unsigned char, 4>& pixel) {
    for (int layer = 0; layer < layerCount; ++layer) {
        writeConstantLayer(texture, firstLayer + layer, tileSize, pixel);
    }
}

std::vector<unsigned char> makeTextureArrayTilePixels(const unsigned char* sourcePixels, const int sourceWidth,
                                                      const int sourceHeight, const int sourceRowStridePixels,
                                                      const int tileSize) {
    const resource::TextureResampleFilter filter =
        resource::selectTextureTileResampleFilter(sourceWidth, sourceHeight, tileSize);
    return resource::resampleRgba8(sourcePixels, sourceWidth, sourceHeight, sourceRowStridePixels, tileSize, tileSize,
                                   filter);
}

void validateMaterialMapTile(const unsigned char* pixels, const size_t pixelCount, const MaterialMapEncoding encoding,
                             const std::filesystem::path& sourcePath) {
    if (encoding == MaterialMapEncoding::LabPbrNormal) {
        return;
    }
    for (size_t pixel = 0u; pixel < pixelCount; ++pixel) {
        const uint8_t encodedMetalId = pixels[pixel * 4u + 1u];
        const auto error = renderer::contracts::validateLabPbrMetalId(encodedMetalId);
        if (error != renderer::contracts::GpuMaterialNormalizationError::None) {
            failBlockTextureArrayBuilder(std::string("Block specular texture violates LabPBR 1.3 [") +
                                         renderer::contracts::gpuMaterialNormalizationErrorStableId(error) +
                                         ", metalId=" + std::to_string(encodedMetalId) + "]: " + sourcePath.string());
        }
    }
}

std::vector<unsigned char> makeMaterialMapTilePixels(const unsigned char* sourcePixels, const int sourceWidth,
                                                     const int sourceHeight, const int sourceRowStridePixels,
                                                     const int tileSize, const MaterialMapEncoding encoding,
                                                     const bool sourceHasAlpha) {
    const resource::TextureResampleFilter filter =
        encoding == MaterialMapEncoding::LabPbrSpecular
            ? resource::TextureResampleFilter::Nearest
            : resource::selectTextureTileResampleFilter(sourceWidth, sourceHeight, tileSize);
    std::vector<unsigned char> tilePixels = resource::resampleRgba8(sourcePixels, sourceWidth, sourceHeight,
                                                                    sourceRowStridePixels, tileSize, tileSize, filter);
    if (encoding == MaterialMapEncoding::LabPbrNormal) {
        renderer::contracts::normalizeLabPbrBlockHeightRange(
            tilePixels.data(), static_cast<size_t>(tileSize) * static_cast<size_t>(tileSize), sourceHasAlpha);
    }
    return tilePixels;
}

void writeAlbedoLayers(PendingTextureArray& texture, const resource::BlockTextureManifestEntry& entry,
                       const int firstLayer, const int layerCount, const int tileSize,
                       const BlockTextureCatalogEntry* catalogEntry, std::vector<glm::vec3>& layerAverageColors) {
    LoadedImage image(entry.albedoPath);
    const ResourceTint tint =
        textureTintColor(catalogEntry != nullptr ? catalogEntry->tint : ResourceTextureTint::None);

    if (layerCount > 1) {
        if (image.height != image.width * layerCount) {
            failBlockTextureArrayBuilder("Texture catalog dimensions do not match texture array source for " +
                                         entry.albedoPath.string());
        }

        const bool topFrameFirst = catalogEntry != nullptr && catalogEntry->topFrameFirst;
        for (int frame = 0; frame < layerCount; ++frame) {
            const int sourceFrameIndex = topFrameFirst ? layerCount - 1 - frame : frame;
            const unsigned char* framePixels =
                image.data + static_cast<size_t>(sourceFrameIndex * image.width * image.width) * 4U;
            const std::vector<unsigned char> tilePixels =
                makeTextureArrayTilePixels(framePixels, image.width, image.width, image.width, tileSize);
            writeTextureArrayLayer(texture, firstLayer + frame, tilePixels.data(), tileSize);
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
    writeTextureArrayLayer(texture, firstLayer, tilePixels.data(), tileSize);
    layerAverageColors[static_cast<size_t>(firstLayer)] =
        computeAlphaWeightedAverageColor(tilePixels.data(), tileSize, tileSize, tint);
}

void writeMaterialMapLayers(PendingTextureArray& texture, const std::optional<std::filesystem::path>& mapPath,
                            const int firstLayer, const int layerCount, const int tileSize, const bool topFrameFirst,
                            const std::array<unsigned char, 4>& neutralPixel, const char* roleName,
                            const MaterialMapEncoding encoding) {
    if (!mapPath.has_value()) {
        writeNeutralMaterialLayers(texture, firstLayer, layerCount, tileSize, neutralPixel);
        return;
    }

    LoadedImage image(mapPath.value());
    const bool sourceHasAlpha = image.channels >= 4;
    validateMaterialMapTile(image.data, static_cast<size_t>(image.width) * static_cast<size_t>(image.height), encoding,
                            mapPath.value());
    if (image.height == image.width) {
        std::vector<unsigned char> tilePixels = makeMaterialMapTilePixels(
            image.data, image.width, image.height, image.width, tileSize, encoding, sourceHasAlpha);
        for (int layer = 0; layer < layerCount; ++layer) {
            writeTextureArrayLayer(texture, firstLayer + layer, tilePixels.data(), tileSize);
        }
        return;
    }

    if (image.height == image.width * layerCount) {
        for (int frame = 0; frame < layerCount; ++frame) {
            const int sourceFrameIndex = topFrameFirst ? layerCount - 1 - frame : frame;
            const unsigned char* framePixels =
                image.data + static_cast<size_t>(sourceFrameIndex * image.width * image.width) * 4U;
            std::vector<unsigned char> tilePixels = makeMaterialMapTilePixels(
                framePixels, image.width, image.width, image.width, tileSize, encoding, sourceHasAlpha);
            writeTextureArrayLayer(texture, firstLayer + frame, tilePixels.data(), tileSize);
        }
        return;
    }

    failBlockTextureArrayBuilder(std::string("Block ") + roleName +
                                 " texture dimensions do not match layer layout for " + mapPath.value().string());
}

} // namespace

namespace resource {

BlockTextureArraySet buildBlockTextureArraySet(const std::string& directory, const int tileSize,
                                               BlockTextureCatalog& catalog, RhiDevice& rhiDevice,
                                               RhiCommandListPool& commandListPool) {
    return buildBlockTextureArraySet(buildBlockTextureManifest(directory), tileSize, catalog, rhiDevice,
                                     commandListPool);
}

BlockTextureArraySet buildBlockTextureArraySet(const BlockTextureManifest& manifest, const int tileSize,
                                               BlockTextureCatalog& catalog, RhiDevice& rhiDevice,
                                               RhiCommandListPool& commandListPool) {
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

    if (numLayers > static_cast<int>(renderer::contracts::kTerrainTextureLayerCount)) {
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
        writeAlbedoLayers(albedoArray, entry, currentLayer, layerCount, tileSize, catalogEntry,
                          result.layerAverageColors);
        if (normalArray.has_value()) {
            writeMaterialMapLayers(*normalArray, entry.normalPath, currentLayer, layerCount, tileSize, topFrameFirst,
                                   kNeutralNormalPixel, "normal", MaterialMapEncoding::LabPbrNormal);
        }
        if (specularArray.has_value()) {
            writeMaterialMapLayers(*specularArray, entry.specularPath, currentLayer, layerCount, tileSize,
                                   topFrameFirst, kNeutralSpecularPixel, "specular",
                                   MaterialMapEncoding::LabPbrSpecular);
        }

        currentLayer += layerCount;
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

    result.albedoArray = albedoArray.create(rhiDevice, "BlockTextures.AlbedoArray");
    if (normalArray.has_value()) {
        result.normalArray = normalArray->create(rhiDevice, "BlockTextures.NormalArray");
    }
    if (specularArray.has_value()) {
        result.specularArray = specularArray->create(rhiDevice, "BlockTextures.SpecularArray");
    }
    RhiCommandList* commandListStorage = commandListPool.acquire(RhiCommandListType::Graphics);
    if (commandListStorage == nullptr ||
        !commandListStorage->begin({"BlockTextureArrays.Commands", RhiCommandListType::Graphics})) {
        std::abort();
    }
    RhiCommandList& commandList = *commandListStorage;
    const auto prepareTexture = [&commandList](const RhiTextureHandle texture, const bool hasMipChain) {
        if (!hasMipChain) {
            commandList.textureBarrier({texture, RhiResourceState::TransferDst, RhiResourceState::ShaderRead});
            return;
        }
        commandList.generateMipmaps(texture);
        commandList.textureBarrier({texture, RhiResourceState::TransferDst, RhiResourceState::ShaderRead});
    };
    const bool hasMipChain = tileSize > 1;
    prepareTexture(result.albedoArray.texture, hasMipChain);
    if (result.normalArray.texture.isValid()) {
        prepareTexture(result.normalArray.texture, hasMipChain);
    }
    if (result.specularArray.texture.isValid()) {
        prepareTexture(result.specularArray.texture, hasMipChain);
    }
    if (!commandList.end()) {
        std::abort();
    }
    RhiCommandList* submittedCommandLists[] = {&commandList};
    if (!rhiDevice.submit({"BlockTextureArrays.Submit", submittedCommandLists, 1u})) {
        std::abort();
    }
    result.albedoPixels = std::move(albedoArray).releasePixels();
    return result;
}

} // namespace resource
