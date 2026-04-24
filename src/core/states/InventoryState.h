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
#include "../../world/DropSystem.h"
#include <cmath>
#include <vector>
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
        m_lastSecondaryPlaceSlot = -1;
        m_primaryDragEmptySlots.clear();
        m_primaryDragging = false;
    }

    void onExit() override {
        returnDraggedItemToInventory();
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
            const auto& dragged = m_deps.input.getUIDragItem();
            if (dragged.active && dragged.itemId > 0) {
                // Right-click with dragged item: place one item into the hovered inventory slot
                handleSecondaryPlace();
                return;
            }
            cancelDraggedItemToSource();
            return;
        }

        // Right held + drag across slots: place one item per slot
        const bool secondaryHeld = m_deps.context.isActionHeld(Action::UISecondaryClick);
        if (secondaryHeld) {
            const auto& dragged = m_deps.input.getUIDragItem();
            if (dragged.active && dragged.itemId > 0) {
                handleSecondaryPlace();
                return;
            }
        }

        // ── Left-button drag-distribute logic ──
        // Use raw mouse button state to detect "held" without interfering with
        // the Pressed-trigger action system (adding a Held binding would cause
        // isActionTriggered(UIPrimaryClick) to fire every frame while held).
        // GLFW_MOUSE_BUTTON_1 == 0 (left mouse button)
        const bool primaryHeld = snapshot.mouseButtons[0];

        // Double-click: when no item is held, stack all same-item slots into the clicked slot
        if (snapshot.mouseButtonsDoubleTapped[0] && primaryPressed) {
            const auto& dragged = m_deps.input.getUIDragItem();
            if (!dragged.active) {
                const int dblCraftingSlot = m_deps.uiRenderer.getCraftingGridLastActivatedSlot();
                const int dblGridIndex = m_deps.uiRenderer.getInventoryPanelLastActivatedSlot();
                if (dblCraftingSlot < 0 && dblGridIndex >= 0) {
                    const int dblSlot = Inventory::toInventoryIndexFromGridSlot(dblGridIndex);
                    Inventory& dblInv = m_deps.player.getInventory();
                    if (dblInv.isValidSlot(dblSlot) && dblInv.getSlotItem(dblSlot) != 0) {
                        handleDoubleClickStack(dblSlot);
                        return;
                    }
                }
            }
        }

        // On release: finalize primary drag distribution
        if (primaryReleased && m_primaryDragging) {
            handlePrimaryDragRelease();
            return;
        }

        // While left is held and we have a dragged item, track hovered empty slots
        if (primaryHeld && m_primaryDragging) {
            const auto& dragged = m_deps.input.getUIDragItem();
            if (dragged.active && dragged.itemId > 0 && dragged.count > 0) {
                trackPrimaryDragSlot();
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

        // Dragged item + left click on an empty slot: start drag-distribute tracking
        if (inventory.getSlotItem(inventorySlot) == 0) {
            m_primaryDragging = true;
            m_primaryDragEmptySlots.clear();
            m_primaryDragEmptySlots.push_back(inventorySlot);
            redistributePrimaryDrag();
            return;
        }

        // Dragged item + left click on an occupied slot: merge if same item, otherwise swap
        const ItemStack targetStack = inventory.getSlotStack(inventorySlot);
        if (targetStack.itemId == static_cast<ItemID>(dragged.itemId)) {
            // Same item: stack onto this slot up to maxStack
            const ItemDef& def = ItemRegistry::get(targetStack.itemId);
            const uint16_t freeSpace = (def.maxStack > targetStack.count)
                ? static_cast<uint16_t>(def.maxStack - targetStack.count) : 0;
            const int toAdd = (dragged.count < freeSpace) ? dragged.count : static_cast<int>(freeSpace);
            if (toAdd > 0) {
                inventory.setSlotItem(inventorySlot, targetStack.itemId,
                    static_cast<uint16_t>(targetStack.count + toAdd));
                const int remaining = dragged.count - toAdd;
                if (remaining <= 0) {
                    m_deps.input.clearUIDragItem();
                } else {
                    m_deps.input.beginUIDragItem(dragged.itemId, remaining, dragged.sourceSlot);
                }
            }
            return;
        }

        // Different items: swap
        inventory.setSlotItem(inventorySlot, static_cast<ItemID>(dragged.itemId), static_cast<uint16_t>(dragged.count));
        m_deps.input.beginUIDragItem(static_cast<int>(targetStack.itemId), static_cast<int>(targetStack.count), inventorySlot);
    }

private:
    static constexpr int kCraftingSlotBase = 10000;

    void returnDraggedItemToInventory() {
        const auto& dragged = m_deps.input.getUIDragItem();
        if (!dragged.active || dragged.itemId <= 0) {
            return;
        }

        Inventory& inventory = m_deps.player.getInventory();
        const ItemID itemId = static_cast<ItemID>(dragged.itemId);
        const uint16_t count = static_cast<uint16_t>(dragged.count);

        // If the item came from an inventory slot, try to put it back there first
        if (dragged.sourceSlot >= 0 && dragged.sourceSlot < kCraftingSlotBase) {
            if (inventory.isValidSlot(dragged.sourceSlot) && inventory.getSlotItem(dragged.sourceSlot) == 0) {
                inventory.setSlotItem(dragged.sourceSlot, itemId, count);
                m_deps.input.clearUIDragItem();
                return;
            }
        }

        // Otherwise, find an empty inventory slot
        for (int slot = 0; slot < Inventory::INVENTORY_SIZE; ++slot) {
            if (inventory.getSlotItem(slot) == 0) {
                inventory.setSlotItem(slot, itemId, count);
                m_deps.input.clearUIDragItem();
                return;
            }
        }

        // No room — spawn a drop item at the player's position
        const glm::vec3 playerPos = m_deps.player.getPosition();
        const glm::ivec3 blockPos(static_cast<int>(std::floor(playerPos.x)),
                                   static_cast<int>(std::floor(playerPos.y)),
                                   static_cast<int>(std::floor(playerPos.z)));
        m_deps.dropSystem.spawnItemDrop(itemId, blockPos, count);
        m_deps.input.clearUIDragItem();
    }

    void cancelDraggedItemToSource() {
        const auto& dragged = m_deps.input.getUIDragItem();
        if (!dragged.active || dragged.itemId <= 0) {
            return;
        }

        if (dragged.sourceSlot >= kCraftingSlotBase) {
            const int craftIdx = dragged.sourceSlot - kCraftingSlotBase;
            if (craftIdx < 4) {
                m_deps.uiRenderer.getCraftingGrid().setCraftingSlot(craftIdx, static_cast<ItemID>(dragged.itemId), static_cast<uint16_t>(dragged.count));
            } else if (craftIdx == 4) {
                m_deps.uiRenderer.getCraftingGrid().setResultSlot(static_cast<ItemID>(dragged.itemId), static_cast<uint16_t>(dragged.count));
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
                    int resultCount = craftGrid.getResultCount();
                    craftGrid.clearAll();
                    m_deps.input.beginUIDragItem(static_cast<int>(resultItem), resultCount, kCraftingSlotBase + 4);
                }
            }
            return;
        }

        if (!dragged.active) {
            // No drag: pick up the item from the crafting slot with its count
            ItemID current = craftGrid.getCraftingSlot(slotIndex);
            if (current != 0) {
                uint16_t currentCount = craftGrid.getCraftingSlotCount(slotIndex);
                craftGrid.setCraftingSlot(slotIndex, 0);
                m_deps.input.beginUIDragItem(static_cast<int>(current), static_cast<int>(currentCount), kCraftingSlotBase + slotIndex);
            }
        } else {
            // Drag active: place dragged item into crafting slot
            ItemID current = craftGrid.getCraftingSlot(slotIndex);
            uint16_t currentCount = craftGrid.getCraftingSlotCount(slotIndex);

            if (current == 0) {
                // Empty slot: start drag-distribute tracking (same as inventory behavior)
                m_primaryDragging = true;
                m_primaryDragEmptySlots.clear();
                m_primaryDragEmptySlots.push_back(kCraftingSlotBase + slotIndex);
                redistributePrimaryDrag();
            } else if (current == static_cast<ItemID>(dragged.itemId)) {
                // Same item: merge up to maxStack
                const ItemDef& def = ItemRegistry::get(current);
                const uint16_t freeSpace = (def.maxStack > currentCount)
                    ? static_cast<uint16_t>(def.maxStack - currentCount) : 0;
                const int toAdd = (dragged.count < freeSpace) ? dragged.count : static_cast<int>(freeSpace);
                if (toAdd > 0) {
                    craftGrid.setCraftingSlot(slotIndex, current,
                        static_cast<uint16_t>(currentCount + toAdd));
                    const int remaining = dragged.count - toAdd;
                    if (remaining <= 0) {
                        m_deps.input.clearUIDragItem();
                    } else {
                        m_deps.input.beginUIDragItem(dragged.itemId, remaining, dragged.sourceSlot);
                    }
                }
            } else {
                // Different items: swap
                craftGrid.setCraftingSlot(slotIndex, static_cast<ItemID>(dragged.itemId), static_cast<uint16_t>(dragged.count));
                m_deps.input.beginUIDragItem(static_cast<int>(current), static_cast<int>(currentCount), kCraftingSlotBase + slotIndex);
            }
        }
    }

    void returnCraftingGridToInventory() {
        CraftingGridControl& craftGrid = m_deps.uiRenderer.getCraftingGrid();
        Inventory& inventory = m_deps.player.getInventory();

        for (int i = 0; i < 4; ++i) {
            ItemID item = craftGrid.getCraftingSlot(i);
            if (item != 0) {
                uint16_t count = craftGrid.getCraftingSlotCount(i);
                if (!tryPlaceItemInInventory(inventory, item, count)) {
                    spawnItemDropAtPlayer(item, count);
                }
            }
        }
        craftGrid.clearAll();
    }

    // Try to place an item into an empty inventory slot. Returns true on success.
    static bool tryPlaceItemInInventory(Inventory& inventory, ItemID itemId, uint16_t count) {
        for (int slot = 0; slot < Inventory::INVENTORY_SIZE; ++slot) {
            if (inventory.getSlotItem(slot) == 0) {
                inventory.setSlotItem(slot, itemId, count);
                return true;
            }
        }
        return false;
    }

    void spawnItemDropAtPlayer(ItemID itemId, uint32_t count) {
        const glm::vec3 playerPos = m_deps.player.getPosition();
        const glm::ivec3 blockPos(static_cast<int>(std::floor(playerPos.x)),
                                   static_cast<int>(std::floor(playerPos.y)),
                                   static_cast<int>(std::floor(playerPos.z)));
        m_deps.dropSystem.spawnItemDrop(itemId, blockPos, count);
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

    // Track a new empty slot encountered while left-dragging with a held item.
    // Immediately redistributes the dragged item count evenly across all tracked slots.
    // Works for both inventory slots and crafting grid slots (0-3).
    void trackPrimaryDragSlot() {
        // Try crafting grid first
        const int hoveredCraftingSlot = m_deps.uiRenderer.getCraftingGridHoveredSlot();
        int unifiedSlot = -1;

        if (hoveredCraftingSlot >= 0 && hoveredCraftingSlot < 4) {
            CraftingGridControl& craftGrid = m_deps.uiRenderer.getCraftingGrid();
            if (craftGrid.getCraftingSlot(hoveredCraftingSlot) == 0) {
                unifiedSlot = kCraftingSlotBase + hoveredCraftingSlot;
            }
        }

        if (unifiedSlot < 0) {
            const int hoveredGridIndex = m_deps.uiRenderer.getInventoryPanelHoveredSlot();
            if (hoveredGridIndex < 0) {
                return;
            }
            const int inventorySlot = Inventory::toInventoryIndexFromGridSlot(hoveredGridIndex);
            Inventory& inventory = m_deps.player.getInventory();
            if (!inventory.isValidSlot(inventorySlot)) {
                return;
            }
            if (inventory.getSlotItem(inventorySlot) != 0) {
                return;
            }
            unifiedSlot = inventorySlot;
        }

        // Only track slots that are not already in the list
        for (int slot : m_primaryDragEmptySlots) {
            if (slot == unifiedSlot) {
                return;
            }
        }
        m_primaryDragEmptySlots.push_back(unifiedSlot);
        redistributePrimaryDrag();
    }

    // Redistribute dragged items evenly across all tracked empty slots.
    // Called each time a new slot is added during left-drag.
    // Supports both inventory slots and crafting grid slots.
    void redistributePrimaryDrag() {
        const auto& dragged = m_deps.input.getUIDragItem();
        if (!dragged.active || dragged.itemId <= 0 || dragged.count <= 0 || m_primaryDragEmptySlots.empty()) {
            return;
        }

        const ItemID itemId = static_cast<ItemID>(dragged.itemId);
        const int total = dragged.count;
        const int n = static_cast<int>(m_primaryDragEmptySlots.size());
        const int base = total / n;
        const int remainder = total % n;

        for (int i = 0; i < n; ++i) {
            const int count = base + (i >= n - remainder ? 1 : 0);
            const int slot = m_primaryDragEmptySlots[i];
            if (slot >= kCraftingSlotBase) {
                m_deps.uiRenderer.getCraftingGrid().setCraftingSlot(
                    slot - kCraftingSlotBase, itemId, static_cast<uint16_t>(count));
            } else {
                m_deps.player.getInventory().setSlotItem(
                    slot, itemId, static_cast<uint16_t>(count));
            }
        }
    }

    // On left-button release: finalize primary drag distribution.
    // All items have already been placed into slots; just clear the drag state.
    void handlePrimaryDragRelease() {
        m_primaryDragging = false;
        if (!m_primaryDragEmptySlots.empty()) {
            m_deps.input.clearUIDragItem();
        }
        m_primaryDragEmptySlots.clear();
    }

    // Double-click on an occupied slot with no item dragged: stack all matching
    // items from other inventory slots into this slot (up to maxStack).
    void handleDoubleClickStack(int targetSlot) {
        Inventory& inventory = m_deps.player.getInventory();
        const ItemID targetItemId = inventory.getSlotItem(targetSlot);
        if (targetItemId == 0) {
            return;
        }

        const ItemDef& def = ItemRegistry::get(targetItemId);
        if (def.maxStack == 0) {
            return;
        }

        uint16_t totalCount = inventory.getSlotStack(targetSlot).count;

        // Scan all other slots and merge matching items into the target
        for (int i = 0; i < Inventory::INVENTORY_SIZE; ++i) {
            if (i == targetSlot) {
                continue;
            }
            if (inventory.getSlotItem(i) != targetItemId) {
                continue;
            }

            if (totalCount >= def.maxStack) {
                break;
            }

            const uint16_t sourceCount = inventory.getSlotStack(i).count;
            const uint16_t freeSpace = def.maxStack - totalCount;
            const uint16_t toMove = (sourceCount <= freeSpace) ? sourceCount : freeSpace;

            totalCount = static_cast<uint16_t>(totalCount + toMove);
            inventory.setSlotItem(targetSlot, targetItemId, totalCount);

            const uint16_t sourceRemaining = static_cast<uint16_t>(sourceCount - toMove);
            if (sourceRemaining == 0) {
                inventory.setSlotItem(i, 0, 0);
            } else {
                inventory.setSlotItem(i, targetItemId, sourceRemaining);
            }
        }
    }

    // Right-click place: put one item from the drag payload into the hovered slot.
    // Also called when right button is held and cursor moves to a new slot (spread-items behavior).
    // Works for both inventory slots and crafting grid slots (0-3, not result).
    void handleSecondaryPlace() {
        // Try crafting grid first
        const int hoveredCraftingSlot = m_deps.uiRenderer.getCraftingGridHoveredSlot();
        int unifiedSlot = -1;

        if (hoveredCraftingSlot >= 0 && hoveredCraftingSlot < 4) {
            CraftingGridControl& craftGrid = m_deps.uiRenderer.getCraftingGrid();
            if (craftGrid.getCraftingSlot(hoveredCraftingSlot) == 0) {
                unifiedSlot = kCraftingSlotBase + hoveredCraftingSlot;
            }
        }

        if (unifiedSlot < 0) {
            const int hoveredGridIndex = m_deps.uiRenderer.getInventoryPanelHoveredSlot();
            if (hoveredGridIndex < 0) {
                return;
            }

            const int inventorySlot = Inventory::toInventoryIndexFromGridSlot(hoveredGridIndex);
            Inventory& inventory = m_deps.player.getInventory();
            if (!inventory.isValidSlot(inventorySlot)) {
                return;
            }
            if (inventory.getSlotItem(inventorySlot) != 0) {
                return;
            }
            unifiedSlot = inventorySlot;
        }

        // Avoid placing into the same slot twice while right button is held
        if (unifiedSlot == m_lastSecondaryPlaceSlot) {
            return;
        }
        m_lastSecondaryPlaceSlot = unifiedSlot;

        const auto& dragged = m_deps.input.getUIDragItem();
        if (!dragged.active || dragged.itemId <= 0 || dragged.count <= 0) {
            return;
        }

        const ItemID itemId = static_cast<ItemID>(dragged.itemId);
        if (unifiedSlot >= kCraftingSlotBase) {
            m_deps.uiRenderer.getCraftingGrid().setCraftingSlot(unifiedSlot - kCraftingSlotBase, itemId, 1);
        } else {
            m_deps.player.getInventory().setSlotItem(unifiedSlot, itemId, 1);
        }

        const int remaining = dragged.count - 1;
        if (remaining <= 0) {
            m_deps.input.clearUIDragItem();
        } else {
            m_deps.input.beginUIDragItem(dragged.itemId, remaining, dragged.sourceSlot);
        }
    }

    StateDependencies m_deps;
    GameplayMode m_gameplayMode = GameplayMode::Survival;
    int m_lastSecondaryPlaceSlot = -1;

    // Left-drag distribute state
    bool m_primaryDragging = false;
    std::vector<int> m_primaryDragEmptySlots;
};

#endif //MECRAFT_INVENTORYSTATE_H
