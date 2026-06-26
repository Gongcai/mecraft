#pragma once

#include <vector>

#include "ContainerUiRegistry.h"
#include "ItemGridControl.h"
#include "../core/UIWidget.h"
#include "../widgets/UITooltip.h"
#include "../../game/inventory/ChestInventoryStore.h"
#include "../../game/inventory/FurnaceInventoryStore.h"

class Inventory;
class Shader;

class DataDrivenContainerPanelControl final : public UIWidget {
public:
    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;

    UIEventResult onInput(const UIInputEvent& event, const UIRenderContext& ctx) override;

    void setVisible(bool isVisible);
    void setDefinition(const ui::ContainerUiDef& definition);
    void setChestSource(const ChestInventory* chest);
    void setFurnaceSource(const FurnaceInventory* furnace);
    void setPlayerInventorySource(const Inventory* inventory);
    void setProgress(float burnFraction, float cookFraction);

    [[nodiscard]] int getContainerLastActivatedSlot() const;
    [[nodiscard]] int getPlayerLastActivatedSlot() const;
    [[nodiscard]] int getContainerHoveredSlot() const;
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

    [[nodiscard]] const ui::ContainerUiDef& requireDefinition() const;
    [[nodiscard]] ResolvedPanelRect resolvePanelRect(int screenWidth, int screenHeight) const;
    [[nodiscard]] int mapContainerGridIndex(int gridIndex) const;
    void syncSlots();
    void appendSlotsForGroup(const ui::ContainerSlotGroupDef& group,
                             const ResolvedPanelRect& panelRect,
                             bool containerGroup,
                             std::vector<Pickable::SlotInfo>& outSlots,
                             std::vector<int>* outSlotMapping) const;
    void renderBackground(const UIRenderContext& context) const;
    void renderProgressBars(const UIRenderContext& context) const;
    void drawTextureQuad(const UIRenderContext& context,
                         float x0,
                         float y0,
                         float x1,
                         float y1,
                         float u0,
                         float v0,
                         float u1,
                         float v1) const;
    void renderDraggedItem(const UIRenderContext& context) const;
    void renderTooltip(const UIRenderContext& context) const;

    const ui::ContainerUiDef* m_definition = nullptr;
    const ChestInventory* m_chest = nullptr;
    const FurnaceInventory* m_furnace = nullptr;
    const Inventory* m_playerInventory = nullptr;
    ItemGridControl m_containerGrid;
    ItemGridControl m_playerGrid;
    std::vector<int> m_containerSlotMapping;

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
