#pragma once

#include <array>

#include "ItemGridControl.h"
#include "../core/UIWidget.h"
#include "../widgets/UITooltip.h"
#include "../../game/inventory/FurnaceInventoryStore.h"

class Inventory;
class Shader;

struct FurnacePanelLayout {
    static constexpr float kTextureWidth = 176.0f;
    static constexpr float kTextureHeight = 166.0f;

    float anchorX = 0.5f;
    float anchorY = 0.5f;
    float offsetX = -88.0f;
    float offsetY = -83.0f;
    float panelScale = 2.0f;

    float inputSlotX = 56.0f;
    float inputSlotY = 17.0f;
    float fuelSlotX = 56.0f;
    float fuelSlotY = 53.0f;
    float outputSlotX = 116.0f;
    float outputSlotY = 35.0f;
    float playerGridOffsetX = 8.0f;
    float playerGridOffsetY = 84.0f;
    float hotbarOffsetY = 142.0f;
    float slotSize = 18.0f;
};

class FurnacePanelControl final : public UIWidget {
public:
    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;

    UIEventResult onInput(const UIInputEvent& event, const UIRenderContext& ctx) override;

    void setVisible(bool isVisible);
    void setFurnaceSource(const FurnaceInventory* furnace);
    void setPlayerInventorySource(const Inventory* inventory);
    void setProgress(float burnFraction, float cookFraction);

    [[nodiscard]] int getFurnaceLastActivatedSlot() const;
    [[nodiscard]] int getPlayerLastActivatedSlot() const;
    [[nodiscard]] int getFurnaceHoveredSlot() const;
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
    void renderProgress(const UIRenderContext& context) const;
    void renderDraggedItem(const UIRenderContext& context) const;
    void renderTooltip(const UIRenderContext& context) const;
    void drawTextureQuad(const UIRenderContext& context,
                         float x0,
                         float y0,
                         float x1,
                         float y1,
                         float u0,
                         float v0,
                         float u1,
                         float v1) const;

    const FurnaceInventory* m_furnace = nullptr;
    const Inventory* m_playerInventory = nullptr;
    FurnacePanelLayout m_layout;
    ItemGridControl m_furnaceGrid;
    ItemGridControl m_playerGrid;

    ResourceMgr* m_resourceMgr = nullptr;
    Shader* m_inventoryShader = nullptr;
    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;
    int m_cachedScreenWidth = 1920;
    int m_cachedScreenHeight = 1080;
    float m_burnFraction = 0.0f;
    float m_cookFraction = 0.0f;

    mutable UITooltip m_tooltip;
    mutable ItemID m_tooltipHoveredItemId = 0;
};
