#pragma once

#include <array>

#include "ItemGridControl.h"
#include "../widgets/UITooltip.h"
#include "../core/UIWidget.h"
#include "../../game/inventory/ChestInventoryStore.h"

class Inventory;
class Shader;

struct ChestPanelLayout {
    static constexpr float kTextureWidth = 177.0f;
    static constexpr float kTextureHeight = 222.0f;

    float anchorX = 0.5f;
    float anchorY = 0.5f;
    float offsetX = -88.5f;
    float offsetY = -111.0f;
    float panelScale = 1.0f;

    float chestGridOffsetX = 8.0f;
    float chestGridOffsetY = 18.0f;
    float playerGridOffsetX = 8.0f;
    float playerGridOffsetY = 140.0f;
    float hotbarOffsetY = 198.0f;
    float slotSize = 18.0f;
    float columnGap = 0.0f;
    float rowGap = 0.0f;
};

class ChestPanelControl : public UIWidget {
public:
    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;

    UIEventResult onInput(const UIInputEvent& event, const UIRenderContext& ctx) override;

    void setVisible(bool isVisible);
    void setChestSource(const ChestInventory* chest);
    void setPlayerInventorySource(const Inventory* inventory);
    void setLayout(const ChestPanelLayout& layout);
    [[nodiscard]] const ChestPanelLayout& getLayout() const;

    [[nodiscard]] int getChestLastActivatedSlot() const;
    [[nodiscard]] int getPlayerLastActivatedSlot() const;
    [[nodiscard]] int getChestHoveredSlot() const;
    [[nodiscard]] int getPlayerHoveredSlot() const;
    void clearActivations();

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
    void syncSlots();
    void renderBackground(const UIRenderContext& context) const;
    void renderDraggedItem(const UIRenderContext& context) const;
    void renderTooltip(const UIRenderContext& context) const;

    const ChestInventory* m_chest = nullptr;
    const Inventory* m_playerInventory = nullptr;
    ChestPanelLayout m_layout;
    ItemGridControl m_chestGrid;
    ItemGridControl m_playerGrid;

    ResourceMgr* m_resourceMgr = nullptr;
    Shader* m_inventoryShader = nullptr;
    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;
    int m_cachedScreenWidth = 1920;
    int m_cachedScreenHeight = 1080;

    mutable UITooltip m_tooltip;
    mutable ItemID m_tooltipHoveredItemId = 0;
};
