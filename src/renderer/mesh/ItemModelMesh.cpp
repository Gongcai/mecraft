#include "ItemModelMesh.h"

#include <array>

namespace {
constexpr float kThickness = 1.0f / 16.0f;
constexpr uint8_t kAlphaThreshold = 10;
// Per-face normals for the extruded item model.
constexpr float kFrontN[3] = { 0.0f,  0.0f,  1.0f};
constexpr float kBackN[3]  = { 0.0f,  0.0f, -1.0f};
constexpr float kLeftN[3]  = {-1.0f,  0.0f,  0.0f};
constexpr float kRightN[3] = { 1.0f,  0.0f,  0.0f};
constexpr float kTopN[3]   = { 0.0f,  1.0f,  0.0f};
constexpr float kBottomN[3]= { 0.0f, -1.0f,  0.0f};

void addQuad(std::vector<ItemModelVertex>& vertices,
             const ItemModelVertex& a,
             const ItemModelVertex& b,
             const ItemModelVertex& c,
             const ItemModelVertex& d) {
    vertices.push_back(a);
    vertices.push_back(b);
    vertices.push_back(c);
    vertices.push_back(a);
    vertices.push_back(c);
    vertices.push_back(d);
}
}

bool buildExtrudedItemMesh(const TextureAtlas& atlas,
                           const std::vector<unsigned char>& atlasPixels,
                           const int tileIndex,
                           std::vector<ItemModelVertex>& outVertices) {
    outVertices.clear();
    if (tileIndex < 0 || !atlas.texture.isValid() || atlas.tilesPerRow <= 0 || atlas.tileSize <= 0 || atlas.atlasWidth <= 0 || atlas.atlasHeight <= 0) {
        return false;
    }

    const int stride = atlas.tileStride > 0 ? atlas.tileStride : atlas.tileSize;
    const int rows = atlas.atlasHeight / stride;
    const int tileCount = atlas.tilesPerRow * rows;
    if (tileIndex >= tileCount) {
        return false;
    }

    const int tileX = tileIndex % atlas.tilesPerRow;
    const int tileY = tileIndex / atlas.tilesPerRow;
    const int startX = tileX * stride + atlas.tilePadding;
    const int startY = tileY * stride + atlas.tilePadding;
    const int tileSize = atlas.tileSize;

    const size_t minPixels = static_cast<size_t>(atlas.atlasWidth) * static_cast<size_t>(atlas.atlasHeight) * 4;
    if (atlasPixels.size() < minPixels) {
        return false;
    }

    std::vector<uint8_t> mask(static_cast<size_t>(tileSize) * tileSize, 0);
    int opaqueCount = 0;
    for (int y = 0; y < tileSize; ++y) {
        for (int x = 0; x < tileSize; ++x) {
            const int px = startX + x;
            const int py = startY + y;
            const size_t idx = static_cast<size_t>(py * atlas.atlasWidth + px) * 4;
            if (atlasPixels[idx + 3] > kAlphaThreshold) {
                mask[static_cast<size_t>(y * tileSize + x)] = 1;
                ++opaqueCount;
            }
        }
    }

    if (opaqueCount == 0) {
        return false;
    }

    outVertices.reserve(static_cast<size_t>(opaqueCount) * 48);

    const float invTile = 1.0f / static_cast<float>(tileSize);
    const float invW = 1.0f / static_cast<float>(atlas.atlasWidth);
    const float invH = 1.0f / static_cast<float>(atlas.atlasHeight);
    const float zBack = 0.5f - kThickness * 0.5f;
    const float zFront = 0.5f + kThickness * 0.5f;

    const auto sampleOpaque = [&](const int x, const int y) {
        if (x < 0 || y < 0 || x >= tileSize || y >= tileSize) {
            return false;
        }
        return mask[static_cast<size_t>(y * tileSize + x)] != 0;
    };

    for (int y = 0; y < tileSize; ++y) {
        for (int x = 0; x < tileSize; ++x) {
            if (!sampleOpaque(x, y)) {
                continue;
            }

            const float x0 = static_cast<float>(x) * invTile;
            const float x1 = static_cast<float>(x + 1) * invTile;
            const float y0 = static_cast<float>(y) * invTile;
            const float y1 = static_cast<float>(y + 1) * invTile;

            const float u0 = static_cast<float>(startX + x) * invW;
            const float u1 = static_cast<float>(startX + x + 1) * invW;
            const float v0 = static_cast<float>(startY + y) * invH;
            const float v1 = static_cast<float>(startY + y + 1) * invH;

            addQuad(outVertices,
                    {x0, y0, zFront, u0, v0, 1.0f, kFrontN[0], kFrontN[1], kFrontN[2]},
                    {x1, y0, zFront, u1, v0, 1.0f, kFrontN[0], kFrontN[1], kFrontN[2]},
                    {x1, y1, zFront, u1, v1, 1.0f, kFrontN[0], kFrontN[1], kFrontN[2]},
                    {x0, y1, zFront, u0, v1, 1.0f, kFrontN[0], kFrontN[1], kFrontN[2]});

            addQuad(outVertices,
                    {x1, y0, zBack, u1, v0, 0.62f, kBackN[0], kBackN[1], kBackN[2]},
                    {x0, y0, zBack, u0, v0, 0.62f, kBackN[0], kBackN[1], kBackN[2]},
                    {x0, y1, zBack, u0, v1, 0.62f, kBackN[0], kBackN[1], kBackN[2]},
                    {x1, y1, zBack, u1, v1, 0.62f, kBackN[0], kBackN[1], kBackN[2]});

            if (!sampleOpaque(x - 1, y)) {
                addQuad(outVertices,
                        {x0, y0, zBack, u0, v0, 0.78f, kLeftN[0], kLeftN[1], kLeftN[2]},
                        {x0, y0, zFront, u0, v0, 0.78f, kLeftN[0], kLeftN[1], kLeftN[2]},
                        {x0, y1, zFront, u0, v1, 0.78f, kLeftN[0], kLeftN[1], kLeftN[2]},
                        {x0, y1, zBack, u0, v1, 0.78f, kLeftN[0], kLeftN[1], kLeftN[2]});
            }

            if (!sampleOpaque(x + 1, y)) {
                addQuad(outVertices,
                        {x1, y0, zFront, u1, v0, 0.85f, kRightN[0], kRightN[1], kRightN[2]},
                        {x1, y0, zBack, u1, v0, 0.85f, kRightN[0], kRightN[1], kRightN[2]},
                        {x1, y1, zBack, u1, v1, 0.85f, kRightN[0], kRightN[1], kRightN[2]},
                        {x1, y1, zFront, u1, v1, 0.85f, kRightN[0], kRightN[1], kRightN[2]});
            }

            if (!sampleOpaque(x, y + 1)) {
                addQuad(outVertices,
                        {x0, y1, zFront, u0, v1, 0.92f, kBottomN[0], kBottomN[1], kBottomN[2]},
                        {x1, y1, zFront, u1, v1, 0.92f, kBottomN[0], kBottomN[1], kBottomN[2]},
                        {x1, y1, zBack, u1, v1, 0.92f, kBottomN[0], kBottomN[1], kBottomN[2]},
                        {x0, y1, zBack, u0, v1, 0.92f, kBottomN[0], kBottomN[1], kBottomN[2]});
            }

            if (!sampleOpaque(x, y - 1)) {
                addQuad(outVertices,
                        {x0, y0, zBack, u0, v0, 0.70f, kTopN[0], kTopN[1], kTopN[2]},
                        {x1, y0, zBack, u1, v0, 0.70f, kTopN[0], kTopN[1], kTopN[2]},
                        {x1, y0, zFront, u1, v0, 0.70f, kTopN[0], kTopN[1], kTopN[2]},
                        {x0, y0, zFront, u0, v0, 0.70f, kTopN[0], kTopN[1], kTopN[2]});
            }
        }
    }

    return !outVertices.empty();
}
