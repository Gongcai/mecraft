#include "TextureAtlas.h"

#include <algorithm>

std::pair<glm::vec2, glm::vec2> TextureAtlas::getUV(const int tileIndex) const {
    if (tilesPerRow == 0 || atlasWidth == 0 || atlasHeight == 0 || tileSize <= 0) {
        return {{0, 0}, {0, 0}};
    }

    const int stride = std::max(1, tileStride);
    const int tileCol = tileIndex % tilesPerRow;
    const int tileRow = tileIndex / tilesPerRow;

    const int startX = tileCol * stride + tilePadding;
    const int startY = tileRow * stride + tilePadding;

    // Use tile boundaries so the full tile area maps onto a face.
    const float innerInsetPx = 1e-3f;
    float uMin = (static_cast<float>(startX) + innerInsetPx) / static_cast<float>(atlasWidth);
    float vMin = (static_cast<float>(startY) + innerInsetPx) / static_cast<float>(atlasHeight);
    float uMax = (static_cast<float>(startX + tileSize) - innerInsetPx) / static_cast<float>(atlasWidth);
    float vMax = (static_cast<float>(startY + tileSize) - innerInsetPx) / static_cast<float>(atlasHeight);

    if (uMin > uMax) {
        const float uCenter = (uMin + uMax) * 0.5f;
        uMin = uCenter;
        uMax = uCenter;
    }
    if (vMin > vMax) {
        const float vCenter = (vMin + vMax) * 0.5f;
        vMin = vCenter;
        vMax = vCenter;
    }

    return {glm::vec2(uMin, vMin), glm::vec2(uMax, vMax)};
}
