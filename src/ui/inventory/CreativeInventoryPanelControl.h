#pragma once

#include <vector>

#include "renderer/rhi/RhiHandles.h"
#include "ItemGridControl.h"
#include "../widgets/UITooltip.h"
#include "../core/UIWidget.h"

class Inventory;
class Shader;

enum class CreativeInventoryTab {
    PlayerInventory,
    AllItems
};

struct CreativeInventoryLayout {
    float panelScale = 2.0f;
    float sourceWidth = 195.0f;
    float sourceHeight = 136.0f;
    float itemGridX = 9.0f;
    float itemGridY = 18.0f;
    float slotSize = 18.0f;
    float scrollbarX = 175.0f;
    float scrollbarY = 18.0f;

    // Player preview frame in source texture pixels, using top-left texture coordinates.
    float playerPreviewX0 = 28.0f;
    float playerPreviewY0 = 6.0f;
    float playerPreviewX1 = 60.0f;
    float playerPreviewY1 = 49.0f;
};

class CreativeInventoryPanelControl : public UIWidget {
public:
    CreativeInventoryPanelControl();

    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;

    UIEventResult onInput(const UIInputEvent& event, const UIRenderContext& ctx) override;

    void setVisible(bool isVisible);
    void setInventorySource(const Inventory* inventory);
    void setTab(CreativeInventoryTab tab);
    [[nodiscard]] CreativeInventoryTab getTab() const;

    void setLayout(const CreativeInventoryLayout& layout);
    [[nodiscard]] const CreativeInventoryLayout& getLayout() const;

    [[nodiscard]] int getLastActivatedSlot() const;
    [[nodiscard]] int getHoveredInventorySlot() const;
    [[nodiscard]] ItemID getLastActivatedCreativeItem() const;
    void clearActivations();

    void setCreativeItemsForTest(const ItemID* itemIds, int count);
    [[nodiscard]] int getScrollRowForTest() const;
    [[nodiscard]] bool isScrollerEnabledForTest() const;

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

    struct HitRect {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
    };

    [[nodiscard]] ResolvedPanelRect resolvePanelRect(int screenWidth, int screenHeight) const;
    [[nodiscard]] HitRect tabRect(CreativeInventoryTab tab, const ResolvedPanelRect& panelRect) const;
    [[nodiscard]] bool hitRectContains(const HitRect& rect, float x, float y) const;
    [[nodiscard]] int maxScrollRow() const;
    [[nodiscard]] bool scrollerEnabled() const;

    void ensureCreativeItems() const;
    void syncSlots();
    void syncInventorySlots(const ResolvedPanelRect& panelRect);
    void syncCreativeSlots(const ResolvedPanelRect& panelRect);
    void clampScrollRow();

    void renderBackground(const UIRenderContext& context) const;
    void renderPlayerPreview(const UIRenderContext& context, const ResolvedPanelRect& panelRect) const;
    void renderTabs(const UIRenderContext& context, const ResolvedPanelRect& panelRect) const;
    void renderScroller(const UIRenderContext& context, const ResolvedPanelRect& panelRect) const;
    void renderDraggedItem(const UIRenderContext& context) const;
    void renderGuiTextureQuad(const UIRenderContext& context,
                              RhiTextureHandle texture,
                              float x,
                              float y,
                              float width,
                              float height,
                              float u0,
                              float v0,
                              float u1,
                              float v1) const;

    const Inventory* m_inventory = nullptr;
    CreativeInventoryLayout m_layout;
    CreativeInventoryTab m_tab = CreativeInventoryTab::PlayerInventory;
    ItemGridControl m_inventoryGrid;
    ItemGridControl m_creativeGrid;
    ItemGridControl m_hotbarGrid;
    mutable std::vector<ItemID> m_creativeItems;
    bool m_useTestCreativeItems = false;
    int m_scrollRow = 0;
    int m_lastActivatedSlot = -1;
    ItemID m_lastActivatedCreativeItem = 0;

    ResourceMgr* m_resourceMgr = nullptr;
    Shader* m_inventoryShader = nullptr;
    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;
    int m_cachedScreenWidth = 1920;
    int m_cachedScreenHeight = 1080;

    mutable UITooltip m_tooltip;
    mutable ItemID m_tooltipHoveredItemId = 0;
};
