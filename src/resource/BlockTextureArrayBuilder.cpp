#include "BlockTextureArrayBuilder.h"

#include "../third_party/stb/stb_image.h"

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <vector>

namespace {

std::vector<std::filesystem::path> collectSortedPngFiles(const std::string& directory) {
    namespace fs = std::filesystem;
    std::vector<fs::path> imagePaths;

    if (!fs::exists(directory)) {
        throw std::runtime_error("Block texture array directory does not exist: " + directory);
    }

    for (const auto& entry : fs::directory_iterator(directory)) {
        if (entry.path().extension() == ".png") {
            imagePaths.push_back(entry.path());
        }
    }

    std::sort(imagePaths.begin(), imagePaths.end(),
              [](const fs::path& a, const fs::path& b) {
                  return a.filename().string() < b.filename().string();
              });

    if (imagePaths.empty()) {
        throw std::runtime_error("Block texture array directory contains no PNG files: " + directory);
    }

    return imagePaths;
}

int computeTextureArrayLayerCount(const std::filesystem::path& imagePath,
                                  const int tileSize,
                                  const BlockTextureCatalog& catalog) {
    const std::string textureName = imagePath.stem().string();
    const BlockTextureCatalogEntry* catalogEntry = catalog.find(textureName);
    if (catalogEntry == nullptr ||
        !catalogEntry->verticalFrames ||
        catalogEntry->animation.frameCount <= 1) {
        return 1;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    if (!stbi_info(imagePath.string().c_str(), &width, &height, &channels)) {
        throw std::runtime_error("Failed to inspect block texture: " + imagePath.string());
    }

    const TextureAnimationInfo& animation = catalogEntry->animation;
    if (width != tileSize || height != tileSize * animation.frameCount) {
        throw std::runtime_error("Texture catalog dimensions do not match vertical frames for " + imagePath.string());
    }

    return animation.frameCount;
}

void uploadTextureArrayLayer(const int targetLayer,
                             const unsigned char* srcPixels,
                             const int tileSize) {
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, targetLayer,
                    tileSize, tileSize, 1, GL_RGBA, GL_UNSIGNED_BYTE, srcPixels);
}

} // namespace

namespace resource {

BlockTextureArray buildBlockTextureArray(const std::string& directory,
                                         const int tileSize,
                                         BlockTextureCatalog& catalog) {
    if (tileSize <= 0) {
        throw std::runtime_error("Block texture array tile size must be positive");
    }

    for (auto& [_, texture] : catalog.entries()) {
        texture.animation.firstLayer = 0;
        if (texture.animation.frameCount > 1) {
            texture.animation.isAnimated = false;
        }
    }

    const std::vector<std::filesystem::path> imagePaths = collectSortedPngFiles(directory);

    std::vector<int> layersPerImage;
    layersPerImage.reserve(imagePaths.size());

    int numLayers = 0;
    for (const std::filesystem::path& imagePath : imagePaths) {
        const int layerCount = computeTextureArrayLayerCount(imagePath, tileSize, catalog);
        layersPerImage.push_back(layerCount);
        numLayers += layerCount;
    }

    if (numLayers > 1024) {
        throw std::runtime_error("Block texture array exceeds the 1024-layer vertex encoding limit");
    }

    BlockTextureArray result;
    result.textureArray.tileSize = tileSize;
    result.textureArray.layerCount = numLayers;

    glGenTextures(1, &result.textureArray.textureID);
    glBindTexture(GL_TEXTURE_2D_ARRAY, result.textureArray.textureID);

    try {
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8,
                     tileSize, tileSize, numLayers,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        stbi_set_flip_vertically_on_load(true);

        int currentLayer = 0;
        for (size_t imageIndex = 0; imageIndex < imagePaths.size(); ++imageIndex) {
            const std::filesystem::path& imagePath = imagePaths[imageIndex];
            const std::string textureName = imagePath.stem().string();
            result.layers[textureName] = currentLayer;

            BlockTextureCatalogEntry* catalogEntry = catalog.findMutable(textureName);
            const int declaredFrames = (catalogEntry != nullptr) ? catalogEntry->animation.frameCount : 1;
            const bool useAnimationFrames = layersPerImage[imageIndex] > 1;
            if (catalogEntry != nullptr) {
                catalogEntry->animation.firstLayer = currentLayer;
                catalogEntry->animation.isAnimated = useAnimationFrames && catalogEntry->animation.frameCount > 1;
            }

            int width = 0;
            int height = 0;
            int channels = 0;
            unsigned char* data = stbi_load(imagePath.string().c_str(), &width, &height, &channels, 4);
            if (!data || width <= 0 || height <= 0) {
                if (data != nullptr) {
                    stbi_image_free(data);
                }
                throw std::runtime_error("Failed to load block texture array source: " + imagePath.string());
            }

            if (useAnimationFrames) {
                if (width != tileSize || height != tileSize * declaredFrames) {
                    stbi_image_free(data);
                    throw std::runtime_error("Texture catalog dimensions do not match texture array source for " + imagePath.string());
                }

                const bool topFrameFirst = catalogEntry != nullptr && catalogEntry->topFrameFirst;
                for (int frame = 0; frame < declaredFrames; ++frame) {
                    const int flippedFrameIndex = topFrameFirst ? declaredFrames - 1 - frame : frame;
                    const unsigned char* framePixels = data + static_cast<size_t>(flippedFrameIndex * tileSize * width) * 4;
                    uploadTextureArrayLayer(currentLayer + frame, framePixels, tileSize);
                }
            } else {
                if (width != tileSize || height != tileSize) {
                    stbi_image_free(data);
                    throw std::runtime_error("Block texture array source dimensions do not match tile size for " + imagePath.string());
                }
                uploadTextureArrayLayer(currentLayer, data, tileSize);
            }

            stbi_image_free(data);
            currentLayer += layersPerImage[imageIndex];
        }

        glGenerateMipmap(GL_TEXTURE_2D_ARRAY);

        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    } catch (...) {
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
        if (result.textureArray.textureID != 0) {
            glDeleteTextures(1, &result.textureArray.textureID);
            result.textureArray.textureID = 0;
        }
        throw;
    }

    int mapLayer = 0;
    for (size_t mapIdx = 0; mapIdx < imagePaths.size(); ++mapIdx) {
        const int atlasTile = static_cast<int>(mapIdx);
        const int layerCount = layersPerImage[mapIdx];
        for (int frame = 0; frame < layerCount; ++frame) {
            result.layerToAtlasTile[mapLayer + frame] = atlasTile;
        }
        mapLayer += layerCount;
    }

    return result;
}

} // namespace resource
