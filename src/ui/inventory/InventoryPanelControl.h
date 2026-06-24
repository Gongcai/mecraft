#pragma once

#include <array>
#include <string>

#include "CraftingGridControl.h"
#include "ItemGridControl.h"
#include "../widgets/UITooltip.h"
#include "../core/UIWidget.h"

class Inventory;
class Shader;
class CraftingSystem;

struct InventoryPanelLayout {
    static constexpr float kTextureWidth = 176.0f;
    static constexpr float kTextureHeight = 166.0f;
    std::string backgroundTextureName = "inventory";
    bool showPlayerPreview = true;

    // Anchor is normalized to current screen size (0..1).
    float anchorX = 0.5f;
    float anchorY = 0.5f;
    // Pixel offset from anchored top-left position.
    float offsetX = -176.0f;
    float offsetY = -166.0f;
    // Uniform scale based on 176x166 source texture.
    float panelScale = 2.0f;

    // Inventory grid layout in source-texture design pixels (scaled by panelScale).
    float gridOffsetX = 6.8f;
    float gridOffsetY = 81.7f;
    float slotSize = 18.0f;
    float columnGap = 0.0f;
    float rowGap = 0.0f;
    float row4ExtraGap = 4.0f;

    // Player preview frame in source texture pixels, using top-left texture coordinates.
    float playerPreviewX0 = 25.0f;
    float playerPreviewY0 = 8.0f;
    float playerPreviewX1 = 78.0f;
    float playerPreviewY1 = 78.0f;

    // Crafting grid layout
    CraftingGridLayout craftingGrid;
};

class InventoryPanelControl : public UIWidget {
public:
    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;

    UIEventResult onInput(const UIInputEvent& event, const UIRenderContext& ctx) override;

    void setVisible(bool isVisible);
    void setSlots(const Pickable::SlotInfo* slots, int count);
    void setInventorySource(const Inventory* inventory);
    void setLayout(const InventoryPanelLayout& layout);
    [[nodiscard]] const InventoryPanelLayout& getLayout() const;
    [[nodiscard]] ItemGridControl& itemGrid();
    [[nodiscard]] const ItemGridControl& itemGrid() const;

    // Crafting grid access
    [[nodiscard]] CraftingGridControl& craftingGrid();
    [[nodiscard]] const CraftingGridControl& craftingGrid() const;

    // Set the crafting system for recipe lookup
    void setCraftingSystem(const CraftingSystem* craftingSystem);

protected:
    void renderSelf(const UIRenderContext& context) const override;

private:
    struct ResolvedPanelRect {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        float scale = 1.0f;
    };

    [[nodiscard]] ResolvedPanelRect resolvePanelRect(int screenWidth, int screenHeight) const;
    void syncSlotsFromInventory();
    void syncCraftingGridPosition(const ResolvedPanelRect& panelRect);
    void renderBackground(const UIRenderContext& context) const;
    void renderPlayerPreview(const UIRenderContext& context, const ResolvedPanelRect& panelRect) const;
    void renderDraggedItem(const UIRenderContext& context) const;

    const Inventory* m_inventory = nullptr;
    const CraftingSystem* m_craftingSystem = nullptr;
    InventoryPanelLayout m_layout;
    ItemGridControl m_itemGrid;
    CraftingGridControl m_craftingGrid;
    bool m_useExternalSlots = false;

    ResourceMgr* m_resourceMgr = nullptr;
    Shader* m_inventoryShader = nullptr;
    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;
    int m_cachedScreenWidth = 1920;
    int m_cachedScreenHeight = 1080;

    // Tooltip
    mutable UITooltip m_tooltip;
    mutable ItemID m_tooltipHoveredItemId = 0;
};

