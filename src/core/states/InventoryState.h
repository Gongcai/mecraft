#ifndef MECRAFT_INVENTORYSTATE_H
#define MECRAFT_INVENTORYSTATE_H

#include "../IGameState.h"
#include "../GameStateMachine.h"
#include "../InputContextManager.h"
#include "GameplayModeRules.h"
#include "StateDependencies.h"
#include "../../player/Player.h"
#include "../../ui/UIRenderer.h"
#include "../../core/Time.h"
class InventoryState final : public IGameState {
public:
    InventoryState(StateDependencies deps,
                   GameplayMode gameplayMode)
        : m_deps(deps),
          m_gameplayMode(gameplayMode) {
    }

    void onEnter() override {
        m_deps.context.pushContext(InputContextType::UI);
        m_deps.input.captureMouse(false);
        m_deps.uiRenderer.setInventoryPanelVisible(true);
    }

    void onExit() override {
        cancelDraggedItemToSource();
        returnCraftingGridToInventory();
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

        if (m_deps.context.isActionTriggered(Action::Inventory) ||
            m_deps.context.isActionTriggered(Action::Menu) ||
            m_deps.context.isActionTriggered(Action::Cancel)) {
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
            cancelDraggedItemToSource();
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
            handleCraftingGridClick(craftingSlot);
            return;
        }

        const int activatedGridIndex = m_deps.uiRenderer.getInventoryPanelLastActivatedSlot();
        const int inventorySlot = Inventory::toInventoryIndexFromGridSlot(activatedGridIndex);
        Inventory& inventory = m_deps.player.getInventory();
        if (!inventory.isValidSlot(inventorySlot)) {
            return;
        }

        const auto& dragged = m_deps.input.getUIDragItem();
        if (!dragged.active) {
            const ItemStack pickedStack = inventory.getSlotStack(inventorySlot);
            if (pickedStack.itemId != 0) {
                inventory.setSlotItem(inventorySlot, 0, 0);
                m_deps.input.beginUIDragItem(static_cast<int>(pickedStack.itemId), static_cast<int>(pickedStack.count), inventorySlot);
            }
            return;
        }

        const ItemStack replacedStack = inventory.getSlotStack(inventorySlot);
        inventory.setSlotItem(inventorySlot, static_cast<ItemID>(dragged.itemId), static_cast<uint16_t>(dragged.count));
        if (replacedStack.itemId == 0) {
            m_deps.input.clearUIDragItem();
        } else {
            m_deps.input.beginUIDragItem(static_cast<int>(replacedStack.itemId), static_cast<int>(replacedStack.count), inventorySlot);
        }
    }

private:
    static constexpr int kCraftingSlotBase = 10000;

    void cancelDraggedItemToSource() {
        const auto& dragged = m_deps.input.getUIDragItem();
        if (!dragged.active || dragged.itemId <= 0) {
            return;
        }

        if (dragged.sourceSlot >= kCraftingSlotBase) {
            const int craftIdx = dragged.sourceSlot - kCraftingSlotBase;
            if (craftIdx < 4) {
                m_deps.uiRenderer.getCraftingGrid().setCraftingSlot(craftIdx, static_cast<ItemID>(dragged.itemId));
            } else if (craftIdx == 4) {
                m_deps.uiRenderer.getCraftingGrid().setResultSlot(static_cast<ItemID>(dragged.itemId));
            }
        } else {
            Inventory& inventory = m_deps.player.getInventory();
            if (inventory.isValidSlot(dragged.sourceSlot)) {
                inventory.setSlotItem(dragged.sourceSlot, static_cast<ItemID>(dragged.itemId), static_cast<uint16_t>(dragged.count));
            }
        }
        m_deps.input.clearUIDragItem();
    }

    void handleCraftingGridClick(int slotIndex) {
        CraftingGridControl& craftGrid = m_deps.uiRenderer.getCraftingGrid();
        const auto& dragged = m_deps.input.getUIDragItem();

        if (slotIndex == 4) {
            if (!dragged.active) {
                ItemID resultItem = craftGrid.getResultSlot();
                if (resultItem != 0) {
                    craftGrid.clearAll();
                    m_deps.input.beginUIDragItem(static_cast<int>(resultItem), 1, kCraftingSlotBase + 4);
                }
            }
            return;
        }

        if (!dragged.active) {
            ItemID current = craftGrid.getCraftingSlot(slotIndex);
            if (current != 0) {
                craftGrid.setCraftingSlot(slotIndex, 0);
                m_deps.input.beginUIDragItem(static_cast<int>(current), 1, kCraftingSlotBase + slotIndex);
            }
        } else {
            ItemID current = craftGrid.getCraftingSlot(slotIndex);
            craftGrid.setCraftingSlot(slotIndex, static_cast<ItemID>(dragged.itemId));
            if (current == 0) {
                m_deps.input.clearUIDragItem();
            } else {
                m_deps.input.beginUIDragItem(static_cast<int>(current), 1, kCraftingSlotBase + slotIndex);
            }
        }
    }

    void returnCraftingGridToInventory() {
        CraftingGridControl& craftGrid = m_deps.uiRenderer.getCraftingGrid();
        Inventory& inventory = m_deps.player.getInventory();

        for (int i = 0; i < 4; ++i) {
            ItemID item = craftGrid.getCraftingSlot(i);
            if (item != 0) {
                bool placed = false;
                for (int slot = 0; slot < Inventory::INVENTORY_SIZE; ++slot) {
                    if (inventory.getSlotItem(slot) == 0) {
                        inventory.setSlotItem(slot, item, 1);
                        placed = true;
                        break;
                    }
                }
                static_cast<void>(placed);
            }
        }
        craftGrid.clearAll();
    }

    void handleHotbarSwap(int hotbarIndex) {
        const int hoveredGridIndex = m_deps.uiRenderer.getInventoryPanelHoveredSlot();
        if (hoveredGridIndex < 0) {
            return;
        }

        const int hoveredSlot = Inventory::toInventoryIndexFromGridSlot(hoveredGridIndex);
        Inventory& inventory = m_deps.player.getInventory();
        if (!inventory.isValidSlot(hoveredSlot)) {
            return;
        }

        cancelDraggedItemToSource();
        inventory.swapSlots(hoveredSlot, hotbarIndex);
    }

    StateDependencies m_deps;
    GameplayMode m_gameplayMode = GameplayMode::Survival;
};

#endif //MECRAFT_INVENTORYSTATE_H
