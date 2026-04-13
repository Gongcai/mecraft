#pragma once

#include <array>

#include "IUIControl.h"
#include "ItemGridControl.h"
#include "../item/Item.h"

class Shader;
class CraftingSystem;

// 2x2 crafting grid + 1 result slot layout parameters
struct CraftingGridLayout {
    static constexpr int GRID_SIZE = 2;        // 2x2 crafting grid
    static constexpr int TOTAL_SLOTS = 5;       // 4 crafting + 1 result

    // Position relative to panel top-left (in source-texture design pixels)
    float offsetX = 87.4f;
    float offsetY = 22.9;
    float slotSize = 18.0f;
    float columnGap = 0.0f;
    float rowGap = 0.0f;

    // Result slot position relative to panel top-left
    float resultOffsetX = 142.5f;
    float resultOffsetY = 33.5f;
    float resultSlotSize = 18.0f;
};

class CraftingGridControl : public IUIControl {
public:
    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;

    void render(const UIRenderContext& context) const override;
    UIEventResult onInput(const UIInputEvent& event) override;
    [[nodiscard]] bool isVisible() const override;

    void setVisible(bool visible);
    void setLayout(const CraftingGridLayout& layout);
    [[nodiscard]] const CraftingGridLayout& getLayout() const;

    // Crafting grid state (4 slots + 1 result)
    [[nodiscard]] ItemID getCraftingSlot(int index) const;
    void setCraftingSlot(int index, ItemID itemId);
    [[nodiscard]] ItemID getResultSlot() const;
    void setResultSlot(ItemID itemId);

    // Clear all crafting slots and result
    void clearAll();

    // Recalculate the crafting result from current grid contents
    void updateCraftingResult(const CraftingSystem& craftingSystem);

    // Get the last activated slot index (0-3 = crafting, 4 = result)
    [[nodiscard]] int getLastActivatedSlot() const;
    [[nodiscard]] int getHoveredSlot() const;
    void clearActivation();

    // Set the panel origin (resolved screen coordinates) for positioning
    void setPanelOrigin(float x, float y, float scale);

private:
    void syncSlotPositions();

    CraftingGridLayout m_layout;
    ItemGridControl m_itemGrid;

    // Crafting grid content: slots 0-3 are 2x2 grid, slot 4 is result
    std::array<ItemID, CraftingGridLayout::TOTAL_SLOTS> m_slots{};

    // Cached panel positioning
    float m_panelX = 0.0f;
    float m_panelY = 0.0f;
    float m_panelScale = 1.0f;

    bool m_visible = false;
    int m_cachedScreenWidth = 1920;
    int m_cachedScreenHeight = 1080;
};
