#ifndef MECRAFT_BITMAPFONT_H
#define MECRAFT_BITMAPFONT_H

#include <utility>

#include <glm/vec2.hpp>

// ASCII bitmap font layout for assets/textures/font/ascii.png
// - Texture size: 128x128
// - Grid: 16x16
// - Ordering: ASCII code order, left-to-right then top-to-bottom
struct BitmapFont {
    static constexpr int kColumns = 16;
    static constexpr int kTextureRows = 8;
    static constexpr int kAsciiRows = 8;
    static constexpr int kGlyphSizePx = 8;

    static std::pair<glm::vec2, glm::vec2> glyphUV(const unsigned char asciiCode) {
        const int index = static_cast<int>(asciiCode & 0x7F);
        const int col = index % kColumns;
        const int rowFromTop = index / kColumns;

        const float invCols = 1.0f / static_cast<float>(kColumns);
        const float invTextureRows = 1.0f / static_cast<float>(kTextureRows);

        const float uMin = static_cast<float>(col) * invCols;
        const float uMax = static_cast<float>(col + 1) * invCols;

        // ASCII glyphs occupy the top half (8 rows) of a 16-row texture.
        const int clampedRow = (rowFromTop < kAsciiRows) ? rowFromTop : (kAsciiRows - 1);
        const float vMax = 1.0f - static_cast<float>(clampedRow) * invTextureRows;
        const float vMin = 1.0f - static_cast<float>(clampedRow + 1) * invTextureRows;

        return {glm::vec2(uMin, vMin), glm::vec2(uMax, vMax)};
    }
};

#endif // MECRAFT_BITMAPFONT_H

