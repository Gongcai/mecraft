#ifndef MECRAFT_INVENTORYSTATE_H
#define MECRAFT_INVENTORYSTATE_H

#include "../IGameState.h"
#include "../GameStateMachine.h"
#include "../InputContextManager.h"
#include "GameplayModeRules.h"
#include "StateDependencies.h"
#include "InventoryDragController.h"
#include "CraftingGridController.h"
#include "../../player/Inventory.h"
#include "../../ui/UIRenderer.h"
#include "../../core/Time.h"
#include "../../world/DropSystem.h"

class InventoryState final : public IGameState {
public:
    InventoryState(StateDependencies deps,
                   GameplayMode /*gameplayMode*/)
        : m_deps(deps),
          m_dragCtrl(m_deps),
          m_craftCtrl(m_deps, m_dragCtrl) {
        m_dragCtrl.setCraftingGrid(&deps.uiRenderer.getCraftingGrid());
    }

    void onEnter() override {
        m_deps.context.pushContext(InputContextType::UI);
        m_deps.input.captureMouse(false);
        m_deps.uiRenderer.setInventoryPanelVisible(true);
        m_dragCtrl.reset();
    }

    void onExit() override {
        m_dragCtrl.returnDraggedItemToInventory();
        m_craftCtrl.returnGridToInventory();
        m_deps.uiRenderer.setInventoryPanelVisible(false);
        m_deps.context.popContext();
        if (m_deps.context.getCurrentContext() == InputContextType::Gameplay) {
            m_deps.input.captureMouse(true);
        }
    }

    void update(float /*dt*/, const InputSnapshot& snapshot) override {
        const bool primaryPressed = m_deps.context.isActionTriggered(Action::UIPrimaryClick);
        const bool secondaryPressed = m_deps.context.isActionTriggered(Action::UISecondaryClick);
        const bool primaryReleased = m_deps.context.isActionTriggered(Action::UIPrimaryRelease);
        const bool secondaryReleased = m_deps.context.isActionTriggered(Action::UISecondaryRelease);

        // Route pointer events to UI widgets
        static_cast<void>(m_deps.uiRenderer.routeUIInput({
            UIInputEventType::PointerMove,
            snapshot.mousePosition.x,
            snapshot.mousePosition.y,
            UIPointerButton::None
        }));

        UIEventResult primaryDownResult = UIEventResult::Ignored;
        if (primaryPressed) {
            primaryDownResult = m_deps.uiRenderer.routeUIInput({
                UIInputEventType::PointerDown,
                snapshot.mousePosition.x,
                snapshot.mousePosition.y,
                UIPointerButton::Primary
            });
        }
        if (secondaryPressed) {
            static_cast<void>(m_deps.uiRenderer.routeUIInput({
                UIInputEventType::PointerDown,
                snapshot.mousePosition.x,
                snapshot.mousePosition.y,
                UIPointerButton::Secondary
            }));
        }
        if (primaryReleased) {
            static_cast<void>(m_deps.uiRenderer.routeUIInput({
                UIInputEventType::PointerUp,
                snapshot.mousePosition.x,
                snapshot.mousePosition.y,
                UIPointerButton::Primary
            }));
        }
        if (secondaryReleased) {
            static_cast<void>(m_deps.uiRenderer.routeUIInput({
                UIInputEventType::PointerUp,
                snapshot.mousePosition.x,
                snapshot.mousePosition.y,
                UIPointerButton::Secondary
            }));
        }

        // Close inventory
        if (m_deps.context.isActionTriggered(Action::Inventory) ||
            m_deps.context.isActionTriggered(Action::Menu) ||
            m_deps.context.isActionTriggered(Action::Cancel)) {
            m_deps.fsm.popState();
            return;
        }

        // Hotbar number key swap
        for (int i = 0; i < Inventory::HOTBAR_SIZE; ++i) {
            const auto hotbarAction = static_cast<Action>(static_cast<int>(Action::Hotbar1) + i);
            if (m_deps.context.isActionTriggered(hotbarAction)) {
                m_dragCtrl.handleHotbarSwap(i);
                return;
            }
        }

        // Right-click: secondary place or cancel drag
        if (secondaryPressed) {
            const auto& dragged = m_deps.input.getUIDragItem();
            if (dragged.active && dragged.itemId > 0) {
                m_dragCtrl.handleSecondaryPlace();
                return;
            }
            m_dragCtrl.cancelDraggedItemToSource();
            return;
        }

        // Right-click held: continue secondary place
        const bool secondaryHeld = m_deps.context.isActionHeld(Action::UISecondaryClick);
        if (secondaryHeld) {
            const auto& dragged = m_deps.input.getUIDragItem();
            if (dragged.active && dragged.itemId > 0) {
                m_dragCtrl.handleSecondaryPlace();
                return;
            }
        }

        const bool primaryHeld = snapshot.mouseButtons[0];

        // Double-click: collect stack
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

        // Primary drag release
        if (primaryReleased && m_dragCtrl.isPrimaryDragging()) {
            m_dragCtrl.handlePrimaryDragRelease();
            return;
        }

        // Primary drag held: track slots
        if (primaryHeld && m_dragCtrl.isPrimaryDragging()) {
            const auto& dragged = m_deps.input.getUIDragItem();
            if (dragged.active && dragged.itemId > 0 && dragged.count > 0) {
                m_dragCtrl.trackPrimaryDragSlot();
            }
            return;
        }

        if (!primaryPressed) return;
        if (primaryDownResult != UIEventResult::Consumed) return;

        // Crafting grid click
        const int craftingSlot = m_deps.uiRenderer.getCraftingGridLastActivatedSlot();
        if (craftingSlot >= 0) {
            m_craftCtrl.handleClick(craftingSlot);
            return;
        }

        // Inventory grid click
        const int activatedGridIndex = m_deps.uiRenderer.getInventoryPanelLastActivatedSlot();
        const int inventorySlot = Inventory::toInventoryIndexFromGridSlot(activatedGridIndex);
        m_dragCtrl.handleSlotClick(inventorySlot);
    }

private:
    StateDependencies m_deps;
    InventoryDragController m_dragCtrl;
    CraftingGridController m_craftCtrl;
};

#endif //MECRAFT_INVENTORYSTATE_H
