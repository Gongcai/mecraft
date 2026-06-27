#ifndef MECRAFT_WORKBENCHSTATE_H
#define MECRAFT_WORKBENCHSTATE_H

#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

#include "../states/IGameState.h"
#include "../states/GameStateMachine.h"
#include "engine/input/InputContextManager.h"
#include "InventoryDragController.h"
#include "CraftingGridController.h"
#include "InventoryStateContext.h"
#include "../../player/Inventory.h"
#include "../../ui/core/UIInputAdapter.h"
#include "../../ui/core/UIRenderer.h"
#include "../../ui/inventory/ContainerUiRegistry.h"

class WorkbenchState final : public IGameState {
public:
    WorkbenchState(InventoryStateContext deps, std::string containerUiId)
        : m_deps(deps),
          m_containerUiId(std::move(containerUiId)),
          m_dragCtrl(m_deps),
          m_craftCtrl(m_deps, m_dragCtrl) {
        m_dragCtrl.setCraftingGrid(&deps.uiRenderer.getCraftingGrid());
    }

    void onEnter() override {
        m_previousLayout = m_deps.uiRenderer.getInventoryPanelLayout();
        m_hasPreviousLayout = true;

        InventoryPanelLayout layout = m_previousLayout;
        applyContainerUiLayout(ui::ContainerUiRegistry::require(m_containerUiId), layout);
        m_deps.uiRenderer.setInventoryPanelLayout(layout);

        m_deps.context.pushContext(InputContextType::UI);
        m_deps.input.captureMouse(false);
        m_deps.uiRenderer.setInventoryPanelVisible(true);
        m_dragCtrl.reset();
    }

    void onExit() override {
        m_dragCtrl.returnDraggedItemToInventory();
        m_craftCtrl.returnGridToInventory();
        m_deps.uiRenderer.setInventoryPanelVisible(false);
        if (m_hasPreviousLayout) {
            m_deps.uiRenderer.setInventoryPanelLayout(m_previousLayout);
        }
        m_deps.context.popContext();
        if (m_deps.context.getCurrentContext() == InputContextType::Gameplay) {
            m_deps.input.captureMouse(true);
        }
    }

    void update(float /*dt*/, const InputSnapshot& snapshot) override {
        const UIInputRouteResult uiRouteResult =
            UIInputAdapter::routeInput(m_deps.uiRenderer, snapshot, m_deps.context);
        const UIEventResult primaryDownResult = uiRouteResult.primaryDown;
        const bool primaryPressed = uiRouteResult.primaryPressed;
        const bool secondaryPressed = uiRouteResult.secondaryPressed;
        const bool primaryReleased = uiRouteResult.primaryReleased;

        if (m_deps.context.isActionTriggered(Action::Inventory) ||
            m_deps.context.isActionTriggered(Action::Menu) ||
            (m_deps.context.isActionTriggered(Action::Cancel) &&
             uiRouteResult.aggregate != UIEventResult::Consumed)) {
            m_deps.fsm.popState();
            return;
        }

        for (int i = 0; i < Inventory::HOTBAR_SIZE; ++i) {
            const auto hotbarAction = static_cast<Action>(static_cast<int>(Action::Hotbar1) + i);
            if (m_deps.context.isActionTriggered(hotbarAction)) {
                m_dragCtrl.handleHotbarSwap(i);
                return;
            }
        }

        if (secondaryPressed) {
            const auto& dragged = m_deps.input.getUIDragItem();
            if (dragged.active && dragged.itemId > 0) {
                m_dragCtrl.handleSecondaryPlace();
                return;
            }
            m_dragCtrl.cancelDraggedItemToSource();
            return;
        }

        const bool secondaryHeld = m_deps.context.isActionHeld(Action::UISecondaryClick);
        if (secondaryHeld) {
            const auto& dragged = m_deps.input.getUIDragItem();
            if (dragged.active && dragged.itemId > 0) {
                m_dragCtrl.handleSecondaryPlace();
                return;
            }
        }

        const bool primaryHeld = snapshot.mouseButtons[0];

        if (snapshot.mouseButtonsDoubleTapped[0] && primaryPressed) {
            const auto& dragged = m_deps.input.getUIDragItem();
            if (!dragged.active) {
                const int dblCraftingSlot = m_deps.uiRenderer.getCraftingGridLastActivatedSlot();
                const int dblGridIndex = m_deps.uiRenderer.getInventoryPanelLastActivatedSlot();
                if (dblCraftingSlot < 0 && dblGridIndex >= 0) {
                    const int dblSlot = Inventory::toInventoryIndexFromGridSlot(dblGridIndex);
                    Inventory& dblInv = m_deps.inventory;
                    if (dblInv.isValidSlot(dblSlot) && dblInv.getSlotItem(dblSlot) != 0) {
                        m_dragCtrl.handleDoubleClickStack(dblSlot);
                        return;
                    }
                }
            }
        }

        if (primaryReleased && m_dragCtrl.isPrimaryDragging()) {
            m_dragCtrl.handlePrimaryDragRelease();
            return;
        }

        if (primaryHeld && m_dragCtrl.isPrimaryDragging()) {
            const auto& dragged = m_deps.input.getUIDragItem();
            if (dragged.active && dragged.itemId > 0 && dragged.count > 0) {
                m_dragCtrl.trackPrimaryDragSlot();
            }
            return;
        }

        if (!primaryPressed) {
            return;
        }
        if (primaryDownResult != UIEventResult::Consumed) {
            return;
        }

        const int craftingSlot = m_deps.uiRenderer.getCraftingGridLastActivatedSlot();
        if (craftingSlot >= 0) {
            m_craftCtrl.handleClick(craftingSlot);
            return;
        }

        const int activatedGridIndex = m_deps.uiRenderer.getInventoryPanelLastActivatedSlot();
        const int inventorySlot = Inventory::toInventoryIndexFromGridSlot(activatedGridIndex);
        m_dragCtrl.handleSlotClick(inventorySlot);
    }

private:
    [[nodiscard]] static bool nearlyEqual(const float lhs, const float rhs) {
        return std::fabs(lhs - rhs) <= 0.001f;
    }

    static void requireEqualMetric(const std::string& owner,
                                   const char* fieldName,
                                   const float actual,
                                   const float expected) {
        if (!nearlyEqual(actual, expected)) {
            throw std::runtime_error(owner + " requires matching field: " + fieldName);
        }
    }

    [[nodiscard]] static const ui::ContainerSlotGroupDef& requireSlotGroup(
        const ui::ContainerUiDef& def,
        const char* id,
        const ui::ContainerSlotGroupKind kind) {
        for (const ui::ContainerSlotGroupDef& group : def.slotGroups) {
            if (group.id != id) {
                continue;
            }
            if (group.kind != kind) {
                throw std::runtime_error(def.id + " has incompatible slot group kind: " + id);
            }
            return group;
        }
        throw std::runtime_error(def.id + " is missing required slot group: " + id);
    }

    static void requireSlotGroupShape(const ui::ContainerUiDef& def,
                                      const ui::ContainerSlotGroupDef& group,
                                      const int firstSlot,
                                      const int columns,
                                      const int rows) {
        if (group.firstSlot != firstSlot ||
            group.columns != columns ||
            group.rows != rows) {
            throw std::runtime_error(def.id + " has incompatible slot group shape: " + group.id);
        }
    }

    static void requireSameSlotMetrics(const ui::ContainerUiDef& def,
                                       const ui::ContainerSlotGroupDef& lhs,
                                       const ui::ContainerSlotGroupDef& rhs) {
        const std::string owner = def.id + "." + rhs.id;
        requireEqualMetric(owner, "x", rhs.x, lhs.x);
        requireEqualMetric(owner, "slotSize", rhs.slotSize, lhs.slotSize);
        requireEqualMetric(owner, "columnGap", rhs.columnGap, lhs.columnGap);
        requireEqualMetric(owner, "rowGap", rhs.rowGap, lhs.rowGap);
    }

    static void applyContainerUiLayout(const ui::ContainerUiDef& def, InventoryPanelLayout& layout) {
        requireEqualMetric(def.id, "width", def.width, InventoryPanelLayout::kTextureWidth);
        requireEqualMetric(def.id, "height", def.height, InventoryPanelLayout::kTextureHeight);

        const ui::ContainerSlotGroupDef& craftingInput =
            requireSlotGroup(def, "crafting_input", ui::ContainerSlotGroupKind::CraftingInput);
        const ui::ContainerSlotGroupDef& craftingResult =
            requireSlotGroup(def, "crafting_result", ui::ContainerSlotGroupKind::CraftingResult);
        const ui::ContainerSlotGroupDef& playerInventory =
            requireSlotGroup(def, "player_inventory", ui::ContainerSlotGroupKind::PlayerInventory);
        const ui::ContainerSlotGroupDef& hotbar =
            requireSlotGroup(def, "hotbar", ui::ContainerSlotGroupKind::PlayerInventory);

        if (craftingInput.columns != craftingInput.rows ||
            craftingInput.columns < CraftingGridLayout::MIN_GRID_SIZE ||
            craftingInput.columns > CraftingGridLayout::MAX_GRID_SIZE) {
            throw std::runtime_error(def.id + " requires square crafting input within supported grid size");
        }
        requireSlotGroupShape(def, craftingResult, craftingInput.columns * craftingInput.rows, 1, 1);
        requireSlotGroupShape(def, playerInventory, Inventory::HOTBAR_SIZE, Inventory::INVENTORY_COLUMNS, Inventory::MAIN_INVENTORY_ROWS);
        requireSlotGroupShape(def, hotbar, 0, Inventory::INVENTORY_COLUMNS, 1);
        requireSameSlotMetrics(def, playerInventory, hotbar);

        const float expectedHotbarY =
            playerInventory.y + static_cast<float>(playerInventory.rows) * (playerInventory.slotSize + playerInventory.rowGap);
        const float row4ExtraGap = hotbar.y - expectedHotbarY;
        if (row4ExtraGap < 0.0f) {
            throw std::runtime_error(def.id + ".hotbar requires y at or below the main inventory rows");
        }

        layout.backgroundTextureName = def.backgroundTexture;
        layout.backgroundAtlasWidth = def.textureWidth;
        layout.backgroundAtlasHeight = def.textureHeight;
        layout.showPlayerPreview = def.showPlayerPreview;
        layout.anchorX = def.anchorX;
        layout.anchorY = def.anchorY;
        layout.pivotX = def.pivotX;
        layout.pivotY = def.pivotY;
        layout.offsetX = def.offsetX;
        layout.offsetY = def.offsetY;
        layout.panelScale = def.scale;
        layout.fitPadding = def.fitPadding;

        layout.gridOffsetX = playerInventory.x;
        layout.gridOffsetY = playerInventory.y;
        layout.slotSize = playerInventory.slotSize;
        layout.columnGap = playerInventory.columnGap;
        layout.rowGap = playerInventory.rowGap;
        layout.row4ExtraGap = row4ExtraGap;

        layout.craftingGrid.gridSize = craftingInput.columns;
        layout.craftingGrid.offsetX = craftingInput.x;
        layout.craftingGrid.offsetY = craftingInput.y;
        layout.craftingGrid.slotSize = craftingInput.slotSize;
        layout.craftingGrid.columnGap = craftingInput.columnGap;
        layout.craftingGrid.rowGap = craftingInput.rowGap;
        layout.craftingGrid.resultOffsetX = craftingResult.x;
        layout.craftingGrid.resultOffsetY = craftingResult.y;
        layout.craftingGrid.resultSlotSize = craftingResult.slotSize;
    }

    InventoryStateContext m_deps;
    std::string m_containerUiId;
    InventoryDragController m_dragCtrl;
    CraftingGridController m_craftCtrl;
    InventoryPanelLayout m_previousLayout;
    bool m_hasPreviousLayout = false;
};

#endif // MECRAFT_WORKBENCHSTATE_H
