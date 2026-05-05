#pragma once

#include <vector>
#include <cmath>
#include <glm/vec3.hpp>

#include "InventorySlotTypes.h"
#include "StateDependencies.h"
#include "../../player/Inventory.h"
#include "../../item/Item.h"
#include "../../ui/UIRenderer.h"
#include "../../ecs/util/PlayerQuery.h"

class CraftingGridControl;

class InventoryDragController {
public:
    explicit InventoryDragController(StateDependencies& deps)
        : m_deps(deps) {}

    void setCraftingGrid(CraftingGridControl* grid) { m_craftGrid = grid; }

    void reset() {
        m_lastSecondaryPlaceSlot = -1;
        m_primaryDragEmptySlots.clear();
        m_primaryDragging = false;
    }

    // Handle left-click on a regular inventory slot (slotIndex in inventory space).
    void handleSlotClick(int inventorySlot) {
        Inventory& inventory = m_deps.inventory;
        if (!inventory.isValidSlot(inventorySlot)) return;

        const auto& dragged = m_deps.input.getUIDragItem();
        if (!dragged.active) {
            const ItemStack pickedStack = inventory.getSlotStack(inventorySlot);
            if (pickedStack.itemId != 0) {
                inventory.setSlotItem(inventorySlot, 0, 0);
                m_deps.input.beginUIDragItem(static_cast<int>(pickedStack.itemId),
                    static_cast<int>(pickedStack.count), inventorySlot);
            }
            return;
        }

        // Dragged item active — place or swap
        if (inventory.getSlotItem(inventorySlot) == 0) {
            beginPrimaryDragToSlot(inventorySlot);
            return;
        }

        const ItemStack targetStack = inventory.getSlotStack(inventorySlot);
        if (targetStack.itemId == static_cast<ItemID>(dragged.itemId)) {
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

        // Swap items
        inventory.setSlotItem(inventorySlot, static_cast<ItemID>(dragged.itemId),
            static_cast<uint16_t>(dragged.count));
        m_deps.input.beginUIDragItem(static_cast<int>(targetStack.itemId),
            static_cast<int>(targetStack.count), inventorySlot);
    }

    // Swap hovered inventory slot with hotbar slot.
    void handleHotbarSwap(int hotbarIndex) {
        const int hoveredGridIndex = m_deps.uiRenderer.getInventoryPanelHoveredSlot();
        if (hoveredGridIndex < 0) return;

        const int hoveredSlot = Inventory::toInventoryIndexFromGridSlot(hoveredGridIndex);
        Inventory& inventory = m_deps.inventory;
        if (!inventory.isValidSlot(hoveredSlot)) return;

        cancelDraggedItemToSource();
        inventory.swapSlots(hoveredSlot, hotbarIndex);
    }

    // Place one item from drag stack into the hovered empty slot (right-click).
    void handleSecondaryPlace() {
        int unifiedSlot = findUnifiedHoveredEmptySlot();
        if (unifiedSlot < 0) return;

        if (unifiedSlot == m_lastSecondaryPlaceSlot) return;
        m_lastSecondaryPlaceSlot = unifiedSlot;

        const auto& dragged = m_deps.input.getUIDragItem();
        if (!dragged.active || dragged.itemId <= 0 || dragged.count <= 0) return;

        const ItemID itemId = static_cast<ItemID>(dragged.itemId);
        if (unifiedSlot >= kCraftingSlotBase) {
            m_craftGrid->setCraftingSlot(unifiedSlot - kCraftingSlotBase, itemId, 1);
        } else {
            m_deps.inventory.setSlotItem(unifiedSlot, itemId, 1);
        }

        const int remaining = dragged.count - 1;
        if (remaining <= 0) {
            m_deps.input.clearUIDragItem();
        } else {
            m_deps.input.beginUIDragItem(dragged.itemId, remaining, dragged.sourceSlot);
        }
    }

    // Return dragged item to its source slot (right-click with no target).
    void cancelDraggedItemToSource() {
        const auto& dragged = m_deps.input.getUIDragItem();
        if (!dragged.active || dragged.itemId <= 0) return;

        if (dragged.sourceSlot >= kCraftingSlotBase) {
            const int craftIdx = dragged.sourceSlot - kCraftingSlotBase;
            if (craftIdx < 4) {
                m_craftGrid->setCraftingSlot(craftIdx, static_cast<ItemID>(dragged.itemId),
                    static_cast<uint16_t>(dragged.count));
            } else if (craftIdx == 4) {
                m_craftGrid->setResultSlot(static_cast<ItemID>(dragged.itemId),
                    static_cast<uint16_t>(dragged.count));
            }
        } else {
            Inventory& inventory = m_deps.inventory;
            if (inventory.isValidSlot(dragged.sourceSlot)) {
                inventory.setSlotItem(dragged.sourceSlot, static_cast<ItemID>(dragged.itemId),
                    static_cast<uint16_t>(dragged.count));
            }
        }
        m_deps.input.clearUIDragItem();
    }

    // Return dragged item to inventory or drop at player's feet.
    void returnDraggedItemToInventory() {
        const auto& dragged = m_deps.input.getUIDragItem();
        if (!dragged.active || dragged.itemId <= 0) return;

        Inventory& inventory = m_deps.inventory;
        const ItemID itemId = static_cast<ItemID>(dragged.itemId);
        const uint16_t count = static_cast<uint16_t>(dragged.count);

        if (dragged.sourceSlot >= 0 && dragged.sourceSlot < kCraftingSlotBase) {
            if (inventory.isValidSlot(dragged.sourceSlot) && inventory.getSlotItem(dragged.sourceSlot) == 0) {
                inventory.setSlotItem(dragged.sourceSlot, itemId, count);
                m_deps.input.clearUIDragItem();
                return;
            }
        }

        for (int slot = 0; slot < Inventory::INVENTORY_SIZE; ++slot) {
            if (inventory.getSlotItem(slot) == 0) {
                inventory.setSlotItem(slot, itemId, count);
                m_deps.input.clearUIDragItem();
                return;
            }
        }

        spawnItemDropAtPlayer(itemId, count);
        m_deps.input.clearUIDragItem();
    }

    // Collect all same-type items into target slot (double-click).
    void handleDoubleClickStack(int targetSlot) {
        Inventory& inventory = m_deps.inventory;
        const ItemID targetItemId = inventory.getSlotItem(targetSlot);
        if (targetItemId == 0) return;

        const ItemDef& def = ItemRegistry::get(targetItemId);
        if (def.maxStack == 0) return;

        uint16_t totalCount = inventory.getSlotStack(targetSlot).count;

        for (int i = 0; i < Inventory::INVENTORY_SIZE; ++i) {
            if (i == targetSlot) continue;
            if (inventory.getSlotItem(i) != targetItemId) continue;
            if (totalCount >= def.maxStack) break;

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

    // Track which empty slots the primary drag has visited.
    void trackPrimaryDragSlot() {
        int unifiedSlot = findUnifiedHoveredEmptySlot();
        if (unifiedSlot < 0) return;

        for (int slot : m_primaryDragEmptySlots) {
            if (slot == unifiedSlot) return;
        }
        m_primaryDragEmptySlots.push_back(unifiedSlot);
        redistributePrimaryDrag();
    }

    // Finalize primary drag on mouse release.
    void handlePrimaryDragRelease() {
        m_primaryDragging = false;
        if (!m_primaryDragEmptySlots.empty()) {
            m_deps.input.clearUIDragItem();
        }
        m_primaryDragEmptySlots.clear();
    }

    [[nodiscard]] bool isPrimaryDragging() const { return m_primaryDragging; }

    // Start primary drag redistribution into a single slot (used by CraftingGridController).
    void beginPrimaryDragToSlot(int unifiedSlot) {
        m_primaryDragging = true;
        m_primaryDragEmptySlots.clear();
        m_primaryDragEmptySlots.push_back(unifiedSlot);
        redistributePrimaryDrag();
    }

private:
    // Find hovered empty slot in unified space (inventory or crafting grid).
    int findUnifiedHoveredEmptySlot() {
        const int hoveredCraftingSlot = m_deps.uiRenderer.getCraftingGridHoveredSlot();
        if (hoveredCraftingSlot >= 0 && hoveredCraftingSlot < 4) {
            if (m_craftGrid->getCraftingSlot(hoveredCraftingSlot) == 0) {
                return kCraftingSlotBase + hoveredCraftingSlot;
            }
        }

        const int hoveredGridIndex = m_deps.uiRenderer.getInventoryPanelHoveredSlot();
        if (hoveredGridIndex < 0) return -1;

        const int inventorySlot = Inventory::toInventoryIndexFromGridSlot(hoveredGridIndex);
        Inventory& inventory = m_deps.inventory;
        if (!inventory.isValidSlot(inventorySlot)) return -1;
        if (inventory.getSlotItem(inventorySlot) != 0) return -1;

        return inventorySlot;
    }

    // Redistribute dragged items evenly across tracked empty slots.
    void redistributePrimaryDrag() {
        const auto& dragged = m_deps.input.getUIDragItem();
        if (!dragged.active || dragged.itemId <= 0 || dragged.count <= 0 || m_primaryDragEmptySlots.empty()) return;

        const ItemID itemId = static_cast<ItemID>(dragged.itemId);
        const int total = dragged.count;
        const int n = static_cast<int>(m_primaryDragEmptySlots.size());
        const int base = total / n;
        const int remainder = total % n;

        for (int i = 0; i < n; ++i) {
            const int count = base + (i >= n - remainder ? 1 : 0);
            const int slot = m_primaryDragEmptySlots[i];
            if (slot >= kCraftingSlotBase) {
                m_craftGrid->setCraftingSlot(slot - kCraftingSlotBase, itemId,
                    static_cast<uint16_t>(count));
            } else {
                m_deps.inventory.setSlotItem(slot, itemId, static_cast<uint16_t>(count));
            }
        }
    }

    void spawnItemDropAtPlayer(ItemID itemId, uint32_t count) {
        ecs::PlayerQuery query(m_deps.ecsRegistry);
        const glm::vec3 playerPos = query.getPosition();
        const glm::ivec3 blockPos(static_cast<int>(std::floor(playerPos.x)),
                                   static_cast<int>(std::floor(playerPos.y)),
                                   static_cast<int>(std::floor(playerPos.z)));
        m_deps.dropSystem.spawnItemDrop(itemId, blockPos, count);
    }

    StateDependencies& m_deps;
    CraftingGridControl* m_craftGrid = nullptr;
    int m_lastSecondaryPlaceSlot = -1;
    bool m_primaryDragging = false;
    std::vector<int> m_primaryDragEmptySlots;
};
