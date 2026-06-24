#pragma once

#include <array>

#include "../core/UIWidget.h"
#include "ItemGridControl.h"
#include "../../item/Item.h"

class Shader;
class CraftingSystem;

struct CraftingGridLayout {
    static constexpr int MIN_GRID_SIZE = 2;
    static constexpr int MAX_GRID_SIZE = 3;
    static constexpr int MAX_CRAFTING_SLOTS = MAX_GRID_SIZE * MAX_GRID_SIZE;
    static constexpr int MAX_TOTAL_SLOTS = MAX_CRAFTING_SLOTS + 1;

    int gridSize = 2;
    float offsetX = 87.4f;
    float offsetY = 22.9f;
    float slotSize = 18.0f;
    float columnGap = 0.0f;
    float rowGap = 0.0f;

    float resultOffsetX = 142.5f;
    float resultOffsetY = 33.5f;
    float resultSlotSize = 18.0f;
};

class CraftingGridControl : public UIWidget {
public:
    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;

    UIEventResult onInput(const UIInputEvent& event, const UIRenderContext& ctx) override;

    void setVisible(bool v);
    void setLayout(const CraftingGridLayout& layout);
    [[nodiscard]] const CraftingGridLayout& getLayout() const;

    // Crafting grid state (4 slots + 1 result)
    [[nodiscard]] ItemID getCraftingSlot(int index) const;
    [[nodiscard]] uint16_t getCraftingSlotCount(int index) const;
    void setCraftingSlot(int index, ItemID itemId, uint16_t count = 1);
    [[nodiscard]] ItemID getResultSlot() const;
    void setResultSlot(ItemID itemId, uint16_t count = 0);
    [[nodiscard]] int getResultCount() const;
    [[nodiscard]] int getGridSize() const;
    [[nodiscard]] int getCraftingCellCount() const;
    [[nodiscard]] int getResultSlotIndex() const;

    // Consume the ingredients for exactly one craft from the current grid.
    [[nodiscard]] bool consumeOneCraft();

    // Clear all crafting slots and result
    void clearAll();

    // Recalculate the crafting result from current grid contents
    void updateCraftingResult(const CraftingSystem& craftingSystem);

    // Get the last activated slot index. The result slot index equals gridSize * gridSize.
    [[nodiscard]] int getLastActivatedSlot() const;
    [[nodiscard]] int getHoveredSlot() const;
    void clearActivation();

    // Set the panel origin (resolved screen coordinates) for positioning
    void setPanelOrigin(float panelX, float panelY, float scale);

protected:
    void renderSelf(const UIRenderContext& context) const override;

private:
    void syncSlotPositions();

    CraftingGridLayout m_layout;
    ItemGridControl m_itemGrid;

    // Crafting grid content: slots 0-8 are inputs, slot 9 stores the result.
    std::array<ItemStack, CraftingGridLayout::MAX_TOTAL_SLOTS> m_slots{};

    // Cached panel positioning
    float m_panelX = 0.0f;
    float m_panelY = 0.0f;
    float m_panelScale = 1.0f;

    int m_cachedScreenWidth = 1920;
    int m_cachedScreenHeight = 1080;
};
