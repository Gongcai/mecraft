#pragma once

#include "../states/IGameState.h"
#include "../states/GameStateMachine.h"
#include "engine//input/InputContextManager.h"
#include "InventoryDragController.h"
#include "InventoryStateContext.h"
#include "../../item/Item.h"
#include "../../player/Inventory.h"
#include "../../ui/core/UIInputAdapter.h"
#include "../../ui/core/UIRenderer.h"

class CreativeInventoryState final : public IGameState {
public:
    explicit CreativeInventoryState(InventoryStateContext deps) : m_deps(deps), m_dragCtrl(m_deps) {
        m_dragCtrl.setCraftingGrid(&deps.uiRenderer.getCraftingGrid());
    }

    void onEnter() override {
        m_deps.context.pushContext(InputContextType::UI);
        m_deps.input.captureMouse(false);
        m_deps.uiRenderer.setInventoryPanelVisible(false);
        m_deps.uiRenderer.setCreativeInventoryTab(CreativeInventoryTab::PlayerInventory);
        m_deps.uiRenderer.setCreativeInventoryVisible(true);
        m_deps.uiRenderer.clearCreativeInventoryActivations();
        m_dragCtrl.reset();
    }

    void onExit() override {
        const auto& dragged = m_deps.input.getUIDragItem();
        if (dragged.active && dragged.sourceSlot == kCreativeDragSourceSlot) {
            m_deps.input.clearUIDragItem();
        } else {
            m_dragCtrl.returnDraggedItemToInventory();
        }
        m_deps.uiRenderer.setCreativeInventoryVisible(false);
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
        const bool primaryReleased = uiRouteResult.primaryReleased;
        const bool secondaryPressed = uiRouteResult.secondaryPressed;

        if (m_deps.context.isActionTriggered(Action::Inventory) || m_deps.context.isActionTriggered(Action::Menu) ||
            (m_deps.context.isActionTriggered(Action::Cancel) && uiRouteResult.aggregate != UIEventResult::Consumed)) {
            m_deps.fsm.popState();
            return;
        }

        for (int i = 0; i < Inventory::HOTBAR_SIZE; ++i) {
            const auto hotbarAction = static_cast<Action>(static_cast<int>(Action::Hotbar1) + i);
            if (m_deps.context.isActionTriggered(hotbarAction)) {
                handleHotbarSwap(i);
                return;
            }
        }

        if (secondaryPressed) {
            const auto& dragged = m_deps.input.getUIDragItem();
            if (dragged.active && dragged.sourceSlot == kCreativeDragSourceSlot) {
                m_deps.input.clearUIDragItem();
                return;
            }
            m_dragCtrl.cancelDraggedItemToSource();
            return;
        }

        if (primaryReleased && m_dragCtrl.isPrimaryDragging()) {
            m_dragCtrl.handlePrimaryDragRelease();
            return;
        }

        const bool primaryHeld = snapshot.mouseButtons[0];
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

        if (m_deps.uiRenderer.getCreativeInventoryTab() == CreativeInventoryTab::AllItems) {
            const int activatedGridIndex = m_deps.uiRenderer.getCreativeInventoryLastActivatedSlot();
            if (activatedGridIndex >= 0) {
                const int inventorySlot = Inventory::toInventoryIndexFromGridSlot(activatedGridIndex);
                m_dragCtrl.handleSlotClick(inventorySlot);
                return;
            }
            handleCreativeItemClick();
            return;
        }

        if (snapshot.mouseButtonsDoubleTapped[0]) {
            const int dblGridIndex = m_deps.uiRenderer.getCreativeInventoryLastActivatedSlot();
            const int dblSlot = Inventory::toInventoryIndexFromGridSlot(dblGridIndex);
            if (m_deps.inventory.isValidSlot(dblSlot) && m_deps.inventory.getSlotItem(dblSlot) != 0) {
                m_dragCtrl.handleDoubleClickStack(dblSlot);
                return;
            }
        }

        const int activatedGridIndex = m_deps.uiRenderer.getCreativeInventoryLastActivatedSlot();
        const int inventorySlot = Inventory::toInventoryIndexFromGridSlot(activatedGridIndex);
        m_dragCtrl.handleSlotClick(inventorySlot);
    }

private:
    static constexpr int kCreativeDragSourceSlot = -1000;

    void handleCreativeItemClick() {
        const ItemID itemId = m_deps.uiRenderer.getCreativeInventoryLastActivatedCreativeItem();
        if (itemId == 0) {
            return;
        }

        const auto& dragged = m_deps.input.getUIDragItem();
        if (dragged.active) {
            if (dragged.sourceSlot == kCreativeDragSourceSlot) {
                m_deps.input.clearUIDragItem();
            } else {
                m_dragCtrl.returnDraggedItemToInventory();
            }
        }

        const ItemDef& def = ItemRegistry::get(itemId);
        const int count = def.maxStack > 0 ? static_cast<int>(def.maxStack) : 1;
        m_deps.input.beginUIDragItem(static_cast<int>(itemId), count, kCreativeDragSourceSlot);
    }

    void handleHotbarSwap(const int hotbarIndex) {
        const int hoveredGridIndex = m_deps.uiRenderer.getCreativeInventoryHoveredInventorySlot();
        if (hoveredGridIndex < 0) {
            return;
        }

        const int hoveredSlot = Inventory::toInventoryIndexFromGridSlot(hoveredGridIndex);
        if (!m_deps.inventory.isValidSlot(hoveredSlot)) {
            return;
        }

        const auto& dragged = m_deps.input.getUIDragItem();
        if (dragged.active && dragged.sourceSlot == kCreativeDragSourceSlot) {
            m_deps.input.clearUIDragItem();
        } else {
            m_dragCtrl.cancelDraggedItemToSource();
        }
        m_deps.inventory.swapSlots(hoveredSlot, hotbarIndex);
    }

    InventoryStateContext m_deps;
    InventoryDragController m_dragCtrl;
};
