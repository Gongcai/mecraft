#ifndef MECRAFT_INVENTORYSTATE_H
#define MECRAFT_INVENTORYSTATE_H

#include "../IGameState.h"
#include "../GameStateMachine.h"
#include "../InputContextManager.h"
#include "GameplayModeRules.h"
#include "../../player/Player.h"
#include "../../ui/UIRenderer.h"
#include "../../core/Time.h"
class InventoryState final : public IGameState {
public:
    InventoryState(GameStateMachine& fsm,
                   Player& player,
                   InputContextManager& context,
                   InputManager& input,
                   UIRenderer& uiRenderer,
                   GameplayMode gameplayMode)
        : m_fsm(fsm),
          m_player(player),
          m_context(context),
          m_input(input),
          m_uiRenderer(uiRenderer),
          m_gameplayMode(gameplayMode) {
    }

    void onEnter() override {
        m_context.pushContext(InputContextType::UI);
        m_input.captureMouse(false);
        m_uiRenderer.setInventoryPanelVisible(true);
    }

    void onExit() override {
        cancelDraggedItemToSource();
        // Closing inventory returns crafting grid items to inventory
        returnCraftingGridToInventory();
        m_uiRenderer.setInventoryPanelVisible(false);
        m_context.popContext();
        if (m_context.getCurrentContext() == InputContextType::Gameplay) {
            m_input.captureMouse(true);
        }
    }

    void update(float /*dt*/, const InputSnapshot& snapshot) override {
        const bool primaryPressed = m_context.isActionTriggered(Action::UIPrimaryClick);
        const bool secondaryPressed = m_context.isActionTriggered(Action::UISecondaryClick);
        const bool primaryReleased = m_context.isActionTriggered(Action::UIPrimaryRelease);
        const bool secondaryReleased = m_context.isActionTriggered(Action::UISecondaryRelease);

        static_cast<void>(m_uiRenderer.routeUIInput({
            UIInputEventType::PointerMove,
            snapshot.mousePosition.x,
            snapshot.mousePosition.y,
            UIPointerButton::None
        }));

        UIEventResult primaryDownResult = UIEventResult::Ignored;
        if (primaryPressed) {
            primaryDownResult = m_uiRenderer.routeUIInput({
                UIInputEventType::PointerDown,
                snapshot.mousePosition.x,
                snapshot.mousePosition.y,
                UIPointerButton::Primary
            });
        }
        if (secondaryPressed) {
            static_cast<void>(m_uiRenderer.routeUIInput({
                UIInputEventType::PointerDown,
                snapshot.mousePosition.x,
                snapshot.mousePosition.y,
                UIPointerButton::Secondary
            }));
        }
        if (primaryReleased) {
            static_cast<void>(m_uiRenderer.routeUIInput({
                UIInputEventType::PointerUp,
                snapshot.mousePosition.x,
                snapshot.mousePosition.y,
                UIPointerButton::Primary
            }));
        }
        if (secondaryReleased) {
            static_cast<void>(m_uiRenderer.routeUIInput({
                UIInputEventType::PointerUp,
                snapshot.mousePosition.x,
                snapshot.mousePosition.y,
                UIPointerButton::Secondary
            }));
        }

        if (m_context.isActionTriggered(Action::Inventory) ||
            m_context.isActionTriggered(Action::Menu) ||
            m_context.isActionTriggered(Action::Cancel)) {
            m_fsm.popState();
            return;
        }

        // 数字键：将鼠标悬浮槽位与对应热栏槽位互换
        for (int i = 0; i < Inventory::HOTBAR_SIZE; ++i) {
            const auto hotbarAction = static_cast<Action>(static_cast<int>(Action::Hotbar1) + i);
            if (m_context.isActionTriggered(hotbarAction)) {
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

        // Check if the click was on the crafting grid (crafting grid has input priority)
        const int craftingSlot = m_uiRenderer.getCraftingGridLastActivatedSlot();
        if (craftingSlot >= 0) {
            handleCraftingGridClick(craftingSlot);
            return;
        }

        const int activatedGridIndex = m_uiRenderer.getInventoryPanelLastActivatedSlot();
        const int inventorySlot = Inventory::toInventoryIndexFromGridSlot(activatedGridIndex);
        Inventory& inventory = m_player.getInventory();
        if (!inventory.isValidSlot(inventorySlot)) {
            return;
        }

        const auto& dragged = m_input.getUIDragItem();
        if (!dragged.active) {
            const ItemStack pickedStack = inventory.getSlotStack(inventorySlot);
                if (pickedStack.itemId != 0) {
                    inventory.setSlotItem(inventorySlot, 0, 0);
                m_input.beginUIDragItem(static_cast<int>(pickedStack.itemId), static_cast<int>(pickedStack.count), inventorySlot);
            }
            return;
        }

        const ItemStack replacedStack = inventory.getSlotStack(inventorySlot);
        inventory.setSlotItem(inventorySlot, static_cast<ItemID>(dragged.itemId), static_cast<uint16_t>(dragged.count));
        if (replacedStack.itemId == 0) {
            m_input.clearUIDragItem();
        } else {
            m_input.beginUIDragItem(static_cast<int>(replacedStack.itemId), static_cast<int>(replacedStack.count), inventorySlot);
        }
    }

private:
    // Slot index encoding for drag source:
    //   >= 0 and < INVENTORY_SIZE: inventory slot
    //   >= kCraftingSlotBase: crafting grid slot (slot - kCraftingSlotBase = crafting index 0-4)
    static constexpr int kCraftingSlotBase = 10000;

    void cancelDraggedItemToSource() {
        const auto& dragged = m_input.getUIDragItem();
        if (!dragged.active || dragged.itemId <= 0) {
            return;
        }

        if (dragged.sourceSlot >= kCraftingSlotBase) {
            // Return to crafting grid
            const int craftIdx = dragged.sourceSlot - kCraftingSlotBase;
            if (craftIdx < 4) {
                m_uiRenderer.getCraftingGrid().setCraftingSlot(craftIdx, static_cast<ItemID>(dragged.itemId));
            } else if (craftIdx == 4) {
                m_uiRenderer.getCraftingGrid().setResultSlot(static_cast<ItemID>(dragged.itemId));
            }
        } else {
            // Return to inventory
            Inventory& inventory = m_player.getInventory();
            if (inventory.isValidSlot(dragged.sourceSlot)) {
                inventory.setSlotItem(dragged.sourceSlot, static_cast<ItemID>(dragged.itemId), static_cast<uint16_t>(dragged.count));
            }
        }
        m_input.clearUIDragItem();
    }

    void handleCraftingGridClick(int slotIndex) {
        // slotIndex: 0-3 = crafting input, 4 = result
        CraftingGridControl& craftGrid = m_uiRenderer.getCraftingGrid();
        const auto& dragged = m_input.getUIDragItem();

        if (slotIndex == 4) {
            // Clicked the result slot — take the result item (only if not dragging)
            if (!dragged.active) {
                ItemID resultItem = craftGrid.getResultSlot();
                if (resultItem != 0) {
                    // Take result: clear crafting input slots, start dragging the result
                    craftGrid.clearAll();
                    m_input.beginUIDragItem(static_cast<int>(resultItem), 1, kCraftingSlotBase + 4);
                }
            }
            return;
        }

        // Crafting input slot (0-3)
        if (!dragged.active) {
            // Pick up item from crafting slot
            ItemID current = craftGrid.getCraftingSlot(slotIndex);
            if (current != 0) {
                craftGrid.setCraftingSlot(slotIndex, 0);
                m_input.beginUIDragItem(static_cast<int>(current), 1, kCraftingSlotBase + slotIndex);
            }
        } else {
            // Place dragged item into crafting slot
            // If there's already an item, swap
            ItemID current = craftGrid.getCraftingSlot(slotIndex);
            craftGrid.setCraftingSlot(slotIndex, static_cast<ItemID>(dragged.itemId));
            if (current == 0) {
                m_input.clearUIDragItem();
            } else {
                m_input.beginUIDragItem(static_cast<int>(current), 1, kCraftingSlotBase + slotIndex);
            }
        }
    }

    void returnCraftingGridToInventory() {
        CraftingGridControl& craftGrid = m_uiRenderer.getCraftingGrid();
        Inventory& inventory = m_player.getInventory();

        for (int i = 0; i < 4; ++i) {
            ItemID item = craftGrid.getCraftingSlot(i);
            if (item != 0) {
                // Try to find an empty slot in inventory
                bool placed = false;
                for (int slot = 0; slot < Inventory::INVENTORY_SIZE; ++slot) {
                    if (inventory.getSlotItem(slot) == 0) {
                        inventory.setSlotItem(slot, item, 1);
                        placed = true;
                        break;
                    }
                }
                // If inventory is full, the item is lost (same as Minecraft behavior)
                static_cast<void>(placed);
            }
        }
        craftGrid.clearAll();
    }

    void handleHotbarSwap(int hotbarIndex) {
        const int hoveredGridIndex = m_uiRenderer.getInventoryPanelHoveredSlot();
        if (hoveredGridIndex < 0) {
            return;
        }

        const int hoveredSlot = Inventory::toInventoryIndexFromGridSlot(hoveredGridIndex);
        Inventory& inventory = m_player.getInventory();
        if (!inventory.isValidSlot(hoveredSlot)) {
            return;
        }

        // 如果有拖拽中的物品，先归还
        cancelDraggedItemToSource();

        inventory.swapSlots(hoveredSlot, hotbarIndex);
    }

    GameStateMachine& m_fsm;
    Player& m_player;
    InputContextManager& m_context;
    InputManager& m_input;
    UIRenderer& m_uiRenderer;
    GameplayMode m_gameplayMode = GameplayMode::Survival;
};

#endif //MECRAFT_INVENTORYSTATE_H

