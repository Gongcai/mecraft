#pragma once

#include <array>
#include <vector>

class ResourceMgr;
class Shader;

// Pure rendering & hit-testing for grid-based pickable slots.
// No item data is stored — the caller provides slot layouts and item IDs at render time.
// All methods are static; this is a stateless utility class.
class Pickable
{
public:
    struct MeshHandles {
        unsigned int vao = 0;
        unsigned int vbo = 0;
    };

    struct SlotInfo {
        int x = 0;          // Top-left pixel X (top-down origin)
        int y = 0;          // Top-left pixel Y (top-down origin)
        int size = 40;      // Width & height (square)
        int itemId = 0;     // 0 = empty slot, no icon drawn
    };

    struct RenderParams {
        std::array<float, 4> hoverBgColor{1.0f, 1.0f, 1.0f, 0.25f};
        std::array<float, 4> iconTintColor{1.0f, 1.0f, 1.0f, 1.0f};
    };

    static void initMesh(MeshHandles& mesh);
    static void shutdownMesh(MeshHandles& mesh);

    // Hit-test: returns the index of the slot under the cursor, or -1 if none.
    // mouseX/mouseY use top-left pixel origin (same as GLFW cursor callbacks).
    static int hitTest(const SlotInfo* slots, int count, float mouseX, float mouseY);

    // Batch-render all slots in two passes:
    //   Pass 1: Semi-transparent background for the hovered slot (crosshair shader, solid color).
    //   Pass 2: Item icons for all non-empty slots (inventory shader, textured from itemIconAtlas).
    // hoveredIndex: slot index currently hovered (-1 = none), from hitTest().
    static void render(const SlotInfo* slots, int count,
                       int hoveredIndex,
                       int screenW, int screenH,
                       const RenderParams& params,
                       Shader* crosshairShader,
                       Shader* inventoryShader,
                       const MeshHandles& mesh,
                       const class ResourceMgr& resourceMgr,
                       const class TextureAtlas& itemIconAtlas,
                       const class TextureAtlas& itemTextureAtlas);
};
