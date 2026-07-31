#ifndef MECRAFT_TEXTURE_ATLAS_H
#define MECRAFT_TEXTURE_ATLAS_H

#include "renderer/rhi/RhiHandles.h"

#include <glm/vec2.hpp>

#include <utility>

struct TextureAtlas {
    RhiTextureHandle texture;
    int atlasWidth = 0; // Atlas width in pixels.
    int atlasHeight = 0; // Atlas height in pixels.
    int tileSize = 16; // Tile size in pixels, excluding padding.
    int tileStride = 16; // Tile cell stride: tileSize + 2 * tilePadding.
    int tilePadding = 0; // Copied edge texel padding around each tile.
    int tilesPerRow = 0;

    // Return UV coordinates for the tile as lower-left and upper-right corners.
    [[nodiscard]] std::pair<glm::vec2, glm::vec2> getUV(int tileIndex) const;
};

struct TextureArray {
    RhiTextureHandle texture;
    int tileSize = 16;
    int layerCount = 0;
};

#endif // MECRAFT_TEXTURE_ATLAS_H
