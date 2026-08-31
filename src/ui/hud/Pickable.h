#pragma once

#include <array>

struct GameResources;
struct TextureAtlas;
struct UIRenderContext;

// Pure rendering & hit-testing for grid-based pickable slots.
// No item data is stored — the caller provides slot layouts and item IDs at render time.
// All methods are static; this is a stateless utility class.
class Pickable {
public:
    struct SlotInfo {
        int x = 0; // Top-left pixel X (top-down origin)
        int y = 0; // Top-left pixel Y (top-down origin)
        int size = 40; // Width & height (square)
        int itemId = 0; // 0 = empty slot, no icon drawn
        int count = 0; // Item stack count (displayed when > 1)
    };

    struct RenderParams {
        std::array<float, 4> hoverBgColor{1.0f, 1.0f, 1.0f, 0.25f};
        std::array<float, 4> iconTintColor{1.0f, 1.0f, 1.0f, 1.0f};

        // Count text layout parameters — all values are ratios of slot size for resolution independence.
        // Actual font scale = countTextScale * slotSize / 8.0 (8 = base glyph pixel size).
        float countTextOffsetX = -0.05f; // Ratio of slot width, from right edge
        float countTextOffsetY = 0.03f; // Ratio of slot height, from bottom edge
        float countTextScale = 0.35f; // Ratio of slot size for text height
    };

    // Hit-test: returns the index of the slot under the cursor, or -1 if none.
    // mouseX/mouseY use top-left pixel origin (same as GLFW cursor callbacks).
    static int hitTest(const SlotInfo* slots, int count, float mouseX, float mouseY);

    // Record the hovered background and item atlas quads into the UI command list, then append
    // item-count glyphs to the existing text batch path. The supplied render context owns the
    // shared quad buffer, pipelines, descriptor cache, scissor, and virtual screen dimensions.
    static void render(const SlotInfo* slots, int count, int hoveredIndex, const RenderParams& params,
                       const UIRenderContext& context, const GameResources& resources,
                       const TextureAtlas& itemIconAtlas, const TextureAtlas& itemTextureAtlas);
};
