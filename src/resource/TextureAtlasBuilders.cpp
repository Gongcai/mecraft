#include "TextureAtlasBuilders.h"
#include "TextureResampler.h"

#include "../third_party/stb/stb_image.h"

#include <glad/glad.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

namespace {

[[noreturn]] void failTextureAtlasBuilders(const std::string& message) {
    std::cerr << message << '\n';
    std::abort();
}

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

std::vector<std::filesystem::path> collectSortedPngFiles(const std::string& directory) {
    namespace fs = std::filesystem;
    std::vector<fs::path> imagePaths;

    std::error_code fsError;
    if (!fs::exists(directory, fsError)) {
        if (fsError) {
            failTextureAtlasBuilders("Failed to inspect texture atlas directory: " +
                                     directory + ": " + fsError.message());
        }
        failTextureAtlasBuilders("Texture atlas directory does not exist: " + directory);
    }

    fs::directory_iterator it(directory, fsError);
    if (fsError) {
        failTextureAtlasBuilders("Failed to iterate texture atlas directory: " +
                                 directory + ": " + fsError.message());
    }
    const fs::directory_iterator end;
    while (it != end) {
        const fs::directory_entry& entry = *it;
        if (entry.path().extension() == ".png") {
            imagePaths.push_back(entry.path());
        }
        it.increment(fsError);
        if (fsError) {
            failTextureAtlasBuilders("Failed to continue iterating texture atlas directory: " +
                                     directory + ": " + fsError.message());
        }
    }

    std::sort(imagePaths.begin(), imagePaths.end(),
              [](const fs::path& a, const fs::path& b) {
                  return a.filename().string() < b.filename().string();
              });

    if (imagePaths.empty()) {
        failTextureAtlasBuilders("Texture atlas directory contains no PNG files: " + directory);
    }

    return imagePaths;
}

void copyTilePadding(std::vector<unsigned char>& atlasPixels,
                     const int atlasWidth,
                     const int innerStartX,
                     const int innerStartY,
                     const int copyWidth,
                     const int copyHeight,
                     const int tilePadding) {
    for (int y = 0; y < copyHeight; ++y) {
        const int srcY = innerStartY + y;
        const int leftSrcX = innerStartX;
        const int rightSrcX = innerStartX + copyWidth - 1;
        for (int p = 1; p <= tilePadding; ++p) {
            const int dstLeftX = innerStartX - p;
            const int dstRightX = innerStartX + copyWidth - 1 + p;
            const int leftSrcIndex = (srcY * atlasWidth + leftSrcX) * 4;
            const int rightSrcIndex = (srcY * atlasWidth + rightSrcX) * 4;
            const int dstLeftIndex = (srcY * atlasWidth + dstLeftX) * 4;
            const int dstRightIndex = (srcY * atlasWidth + dstRightX) * 4;
            for (int c = 0; c < 4; ++c) {
                atlasPixels[dstLeftIndex + c] = atlasPixels[leftSrcIndex + c];
                atlasPixels[dstRightIndex + c] = atlasPixels[rightSrcIndex + c];
            }
        }
    }

    for (int x = -tilePadding; x < copyWidth + tilePadding; ++x) {
        const int srcX = innerStartX + std::clamp(x, 0, copyWidth - 1);
        const int topSrcY = innerStartY;
        const int bottomSrcY = innerStartY + copyHeight - 1;
        for (int p = 1; p <= tilePadding; ++p) {
            const int dstTopY = innerStartY - p;
            const int dstBottomY = innerStartY + copyHeight - 1 + p;
            const int topSrcIndex = (topSrcY * atlasWidth + srcX) * 4;
            const int bottomSrcIndex = (bottomSrcY * atlasWidth + srcX) * 4;
            const int dstTopIndex = (dstTopY * atlasWidth + srcX) * 4;
            const int dstBottomIndex = (dstBottomY * atlasWidth + srcX) * 4;
            for (int c = 0; c < 4; ++c) {
                atlasPixels[dstTopIndex + c] = atlasPixels[topSrcIndex + c];
                atlasPixels[dstBottomIndex + c] = atlasPixels[bottomSrcIndex + c];
            }
        }
    }
}

void copyTilePixelsToAtlas(std::vector<unsigned char>& atlasPixels,
                           const int atlasWidth,
                           const int innerStartX,
                           const int innerStartY,
                           const int tileSize,
                           const std::vector<unsigned char>& tilePixels) {
    if (tilePixels.size() != static_cast<size_t>(tileSize) * static_cast<size_t>(tileSize) * 4U) {
        failTextureAtlasBuilders("Block texture atlas tile pixel buffer size is invalid");
    }
    for (int y = 0; y < tileSize; ++y) {
        for (int x = 0; x < tileSize; ++x) {
            const size_t destIndex = static_cast<size_t>((innerStartY + y) * atlasWidth + (innerStartX + x)) * 4U;
            const size_t sourceIndex = static_cast<size_t>(y * tileSize + x) * 4U;
            atlasPixels[destIndex + 0U] = tilePixels[sourceIndex + 0U];
            atlasPixels[destIndex + 1U] = tilePixels[sourceIndex + 1U];
            atlasPixels[destIndex + 2U] = tilePixels[sourceIndex + 2U];
            atlasPixels[destIndex + 3U] = tilePixels[sourceIndex + 3U];
        }
    }
}

void uploadNearestAtlasTexture(const TextureAtlas& atlas,
                               const std::vector<unsigned char>& pixels,
                               const bool generateMipmaps,
                               const int maxMipmapLevel) {
    glBindTexture(GL_TEXTURE_2D, atlas.textureID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, generateMipmaps ? GL_NEAREST_MIPMAP_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, atlas.atlasWidth, atlas.atlasHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    if (generateMipmaps) {
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, maxMipmapLevel);
    glBindTexture(GL_TEXTURE_2D, 0);
}

} // namespace

namespace resource {

IndexedTextureAtlas buildItemTextureAtlas(const std::string& directory,
                                          const int tileSize,
                                          const BlockTextureCatalog& catalog) {
    if (tileSize <= 0) {
        failTextureAtlasBuilders("Item texture atlas tile size must be positive");
    }

    const std::vector<std::filesystem::path> imagePaths = collectSortedPngFiles(directory);
    const int numTiles = static_cast<int>(imagePaths.size());
    const int tilesPerRow = static_cast<int>(std::ceil(std::sqrt(static_cast<float>(numTiles))));
    const int numRows = static_cast<int>(std::ceil(static_cast<float>(numTiles) / static_cast<float>(tilesPerRow)));
    constexpr int kTilePadding = 1;
    const int tileStride = tileSize + kTilePadding * 2;
    const int atlasWidth = tilesPerRow * tileStride;
    const int atlasHeight = numRows * tileStride;

    IndexedTextureAtlas result;
    result.pixels.assign(static_cast<size_t>(atlasWidth) * static_cast<size_t>(atlasHeight) * 4, 0);

    stbi_set_flip_vertically_on_load(true);

    for (int i = 0; i < numTiles; ++i) {
        int width = 0;
        int height = 0;
        int channels = 0;
        unsigned char* data = stbi_load(imagePaths[i].string().c_str(), &width, &height, &channels, 4);
        if (!data || width <= 0 || height <= 0) {
            if (data != nullptr) {
                stbi_image_free(data);
            }
            failTextureAtlasBuilders("Failed to load item texture: " + imagePaths[i].string());
        }

        const std::string textureName = imagePaths[i].stem().string();
        const ResourceTint tint = textureTintColor(catalog.tintFor(textureName));
        const int copyWidth = std::min(tileSize, width);
        const int copyHeight = std::min(tileSize, height);

        const int tileCol = i % tilesPerRow;
        const int tileRow = i / tilesPerRow;
        const int tileBaseX = tileCol * tileStride;
        const int tileBaseY = tileRow * tileStride;
        const int innerStartX = tileBaseX + kTilePadding;
        const int innerStartY = tileBaseY + kTilePadding;

        for (int y = 0; y < copyHeight; ++y) {
            for (int x = 0; x < copyWidth; ++x) {
                const int dstIndex = ((innerStartY + y) * atlasWidth + (innerStartX + x)) * 4;
                const int srcIndex = (y * width + x) * 4;
                result.pixels[dstIndex + 0] = static_cast<unsigned char>(
                    std::round(std::clamp(static_cast<float>(data[srcIndex + 0]) * tint.r, 0.0f, 255.0f)));
                result.pixels[dstIndex + 1] = static_cast<unsigned char>(
                    std::round(std::clamp(static_cast<float>(data[srcIndex + 1]) * tint.g, 0.0f, 255.0f)));
                result.pixels[dstIndex + 2] = static_cast<unsigned char>(
                    std::round(std::clamp(static_cast<float>(data[srcIndex + 2]) * tint.b, 0.0f, 255.0f)));
                result.pixels[dstIndex + 3] = data[srcIndex + 3];
            }
        }

        copyTilePadding(result.pixels, atlasWidth, innerStartX, innerStartY, copyWidth, copyHeight, kTilePadding);

        stbi_image_free(data);
        result.indices[textureName] = i;
    }

    glGenTextures(1, &result.atlas.textureID);
    result.atlas.atlasWidth = atlasWidth;
    result.atlas.atlasHeight = atlasHeight;
    result.atlas.tileSize = tileSize;
    result.atlas.tileStride = tileStride;
    result.atlas.tilePadding = kTilePadding;
    result.atlas.tilesPerRow = tilesPerRow;
    uploadNearestAtlasTexture(result.atlas, result.pixels, true, 0);
    return result;
}

IndexedTextureAtlas buildBlockTextureAtlas(const std::string& directory,
                                           const int tileSize,
                                           const BlockTextureCatalog& catalog) {
    return buildBlockTextureAtlas(buildBlockTextureManifest(directory), tileSize, catalog);
}

IndexedTextureAtlas buildBlockTextureAtlas(const BlockTextureManifest& manifest,
                                           const int tileSize,
                                           const BlockTextureCatalog& catalog) {
    if (tileSize <= 0) {
        failTextureAtlasBuilders("Block texture atlas tile size must be positive");
    }

    const std::vector<BlockTextureManifestEntry>& textureEntries = manifest.entries();
    if (textureEntries.empty()) {
        failTextureAtlasBuilders("Block texture atlas manifest contains no albedo textures");
    }

    const int numTiles = static_cast<int>(textureEntries.size());
    const int tilesPerRow = static_cast<int>(std::ceil(std::sqrt(static_cast<float>(numTiles))));
    const int numRows = static_cast<int>(std::ceil(static_cast<float>(numTiles) / static_cast<float>(tilesPerRow)));
    constexpr int kTilePadding = 2;
    const int tileStride = tileSize + kTilePadding * 2;
    const int atlasWidth = tilesPerRow * tileStride;
    const int atlasHeight = numRows * tileStride;

    IndexedTextureAtlas result;
    result.pixels.assign(static_cast<size_t>(atlasWidth) * static_cast<size_t>(atlasHeight) * 4, 0);

    stbi_set_flip_vertically_on_load(true);

    for (int i = 0; i < numTiles; ++i) {
        const BlockTextureManifestEntry& textureEntry = textureEntries[static_cast<size_t>(i)];
        int width = 0;
        int height = 0;
        int channels = 0;
        unsigned char* data = stbi_load(textureEntry.albedoPath.string().c_str(), &width, &height, &channels, 4);
        if (!data || width <= 0 || height <= 0) {
            if (data != nullptr) {
                stbi_image_free(data);
            }
            failTextureAtlasBuilders("Failed to load block texture: " + textureEntry.albedoPath.string());
        }

        const std::string& textureName = textureEntry.name;
        const BlockTextureCatalogEntry* catalogEntry = catalog.find(textureName);
        const bool useVerticalFrame =
            catalogEntry != nullptr &&
            catalogEntry->verticalFrames &&
            catalogEntry->animation.frameCount > 1;
        int sourceHeight = height;
        const unsigned char* sourcePixels = data;

        if (useVerticalFrame) {
            const int frameCount = catalogEntry->animation.frameCount;
            if (height != width * frameCount) {
                stbi_image_free(data);
                failTextureAtlasBuilders("Texture catalog dimensions do not match atlas source for " + textureEntry.albedoPath.string());
            }
            const int frameIndex = catalogEntry->topFrameFirst ? frameCount - 1 : 0;
            sourcePixels = data + static_cast<size_t>(frameIndex * width * width) * 4U;
            sourceHeight = width;
        } else if (height != width) {
            stbi_image_free(data);
            failTextureAtlasBuilders("Block texture must be square for " + textureEntry.albedoPath.string());
        }

        const int tileCol = i % tilesPerRow;
        const int tileRow = i / tilesPerRow;
        const int tileBaseX = tileCol * tileStride;
        const int tileBaseY = tileRow * tileStride;
        const int innerStartX = tileBaseX + kTilePadding;
        const int innerStartY = tileBaseY + kTilePadding;

        const TextureResampleFilter filter = selectTextureTileResampleFilter(width, sourceHeight, tileSize);
        const std::vector<unsigned char> tilePixels = resampleRgba8(sourcePixels,
                                                                    width,
                                                                    sourceHeight,
                                                                    width,
                                                                    tileSize,
                                                                    tileSize,
                                                                    filter);
        copyTilePixelsToAtlas(result.pixels, atlasWidth, innerStartX, innerStartY, tileSize, tilePixels);

        copyTilePadding(result.pixels, atlasWidth, innerStartX, innerStartY, tileSize, tileSize, kTilePadding);

        stbi_image_free(data);
        result.indices[textureName] = i;
    }

    glGenTextures(1, &result.atlas.textureID);
    result.atlas.atlasWidth = atlasWidth;
    result.atlas.atlasHeight = atlasHeight;
    result.atlas.tileSize = tileSize;
    result.atlas.tileStride = tileStride;
    result.atlas.tilePadding = kTilePadding;
    result.atlas.tilesPerRow = tilesPerRow;

    const int fullChainMaxLevel = static_cast<int>(std::floor(std::log2(static_cast<float>(std::max(atlasWidth, atlasHeight)))));
    const int paddingSafeMaxLevel = static_cast<int>(std::floor(std::log2(static_cast<float>(kTilePadding))));
    const int clampedMaxLevel = std::max(0, std::min(fullChainMaxLevel, paddingSafeMaxLevel));
    uploadNearestAtlasTexture(result.atlas, result.pixels, true, clampedMaxLevel);
    return result;
}

IndexedTextureAtlas buildHudIconAtlas(const std::string& directory, const int iconSize) {
    if (iconSize <= 0) {
        failTextureAtlasBuilders("HUD icon atlas icon size must be positive");
    }

    const std::vector<std::filesystem::path> imagePaths = collectSortedPngFiles(directory);
    const int numTiles = static_cast<int>(imagePaths.size());
    const int tilesPerRow = numTiles;
    constexpr int kTilePadding = 0;
    const int tileStride = iconSize + kTilePadding * 2;
    const int atlasWidth = tilesPerRow * tileStride;
    const int atlasHeight = tileStride;

    IndexedTextureAtlas result;
    result.pixels.assign(static_cast<size_t>(atlasWidth) * static_cast<size_t>(atlasHeight) * 4, 0);

    stbi_set_flip_vertically_on_load(true);

    for (int i = 0; i < numTiles; ++i) {
        int width = 0;
        int height = 0;
        int channels = 0;
        unsigned char* data = stbi_load(imagePaths[i].string().c_str(), &width, &height, &channels, 4);
        if (!data || width <= 0 || height <= 0) {
            if (data != nullptr) {
                stbi_image_free(data);
            }
            failTextureAtlasBuilders("Failed to load HUD icon texture: " + imagePaths[i].string());
        }

        const int copyWidth = std::min(iconSize, width);
        const int copyHeight = std::min(iconSize, height);

        const int innerStartX = i * tileStride + kTilePadding;
        const int innerStartY = kTilePadding;

        for (int y = 0; y < copyHeight; ++y) {
            for (int x = 0; x < copyWidth; ++x) {
                const int dstIndex = ((innerStartY + y) * atlasWidth + (innerStartX + x)) * 4;
                const int srcIndex = (y * width + x) * 4;
                result.pixels[dstIndex + 0] = data[srcIndex + 0];
                result.pixels[dstIndex + 1] = data[srcIndex + 1];
                result.pixels[dstIndex + 2] = data[srcIndex + 2];
                result.pixels[dstIndex + 3] = data[srcIndex + 3];
            }
        }

        stbi_image_free(data);
        result.indices[imagePaths[i].stem().string()] = i;
    }

    glGenTextures(1, &result.atlas.textureID);
    result.atlas.atlasWidth = atlasWidth;
    result.atlas.atlasHeight = atlasHeight;
    result.atlas.tileSize = iconSize;
    result.atlas.tileStride = tileStride;
    result.atlas.tilePadding = kTilePadding;
    result.atlas.tilesPerRow = tilesPerRow;
    uploadNearestAtlasTexture(result.atlas, result.pixels, false, 0);
    return result;
}

} // namespace resource
