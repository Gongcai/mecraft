#ifndef MECRAFT_WORKBENCHSTATE_H
#define MECRAFT_WORKBENCHSTATE_H

#include "../states/IGameState.h"
#include "../states/GameStateMachine.h"
#include "engine/input/InputContextManager.h"
#include "CraftingGridController.h"
#include "InventoryDragController.h"
#include "InventoryStateContext.h"
#include "../../player/Inventory.h"
#include "../../ui/core/UIInputAdapter.h"
#include "../../ui/core/UIRenderer.h"

class WorkbenchState final : public IGameState {
public:
    explicit WorkbenchState(InventoryStateContext deps)
        : m_deps(deps),
          m_dragCtrl(m_deps),
          m_craftCtrl(m_deps, m_dragCtrl) {
        m_dragCtrl.setCraftingGrid(&deps.uiRenderer.getCraftingGrid());
    }

    void onEnter() override {
        m_previousLayout = m_deps.uiRenderer.getInventoryPanelLayout();
        m_hasPreviousLayout = true;

        InventoryPanelLayout layout = m_previousLayout;
        layout.backgroundTextureName = "crafting_table";
        layout.showPlayerPreview = false;
        layout.gridOffsetX = 6.8f;
        layout.gridOffsetY = 84.0f;
        layout.row4ExtraGap = 4.0f;
        layout.craftingGrid.gridSize = 3;
        layout.craftingGrid.offsetX = 29.5f;
        layout.craftingGrid.offsetY = 16.5f;
        layout.craftingGrid.resultOffsetX = 123.5f;
        layout.craftingGrid.resultOffsetY = 34.5f;
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
    InventoryStateContext m_deps;
    InventoryDragController m_dragCtrl;
    CraftingGridController m_craftCtrl;
    InventoryPanelLayout m_previousLayout;
    bool m_hasPreviousLayout = false;
};

#endif // MECRAFT_WORKBENCHSTATE_H
