#pragma once

#include <cmath>
#include <glm/vec3.hpp>

#include "InventorySlotTypes.h"
#include "InventoryStateContext.h"
#include "../../ecs/util/PlayerQuery.h"
#include "../../player/Inventory.h"
#include "../../item/Item.h"
#include "../../ui/core/UIRenderer.h"
#include "../../ui/inventory/CraftingGridControl.h"
#include "../../world/DropSystem.h"

class InventoryDragController;

class CraftingGridController {
public:
    CraftingGridController(InventoryStateContext& deps, InventoryDragController& dragCtrl)
        : m_deps(deps), m_dragCtrl(dragCtrl) {}

    // Handle click on a crafting grid slot (0-3 = grid, 4 = result).
    void handleClick(int slotIndex) {
        CraftingGridControl& craftGrid = m_deps.uiRenderer.getCraftingGrid();
        const auto& dragged = m_deps.input.getUIDragItem();

        if (slotIndex == 4) {
            // Result slot: pick up crafted item
            if (!dragged.active) {
                ItemID resultItem = craftGrid.getResultSlot();
                if (resultItem != 0) {
                    int resultCount = craftGrid.getResultCount();
                    craftGrid.clearAll();
                    m_deps.input.beginUIDragItem(static_cast<int>(resultItem),
                        resultCount, kCraftingSlotBase + 4);
                }
            }
            return;
        }

        if (!dragged.active) {
            // Pick up from crafting slot
            ItemID current = craftGrid.getCraftingSlot(slotIndex);
            if (current != 0) {
                uint16_t currentCount = craftGrid.getCraftingSlotCount(slotIndex);
                craftGrid.setCraftingSlot(slotIndex, 0);
                m_deps.input.beginUIDragItem(static_cast<int>(current),
                    static_cast<int>(currentCount), kCraftingSlotBase + slotIndex);
            }
        } else {
            // Place or swap into crafting slot
            ItemID current = craftGrid.getCraftingSlot(slotIndex);
            uint16_t currentCount = craftGrid.getCraftingSlotCount(slotIndex);

            if (current == 0) {
                m_dragCtrl.beginPrimaryDragToSlot(kCraftingSlotBase + slotIndex);
            } else if (current == static_cast<ItemID>(dragged.itemId)) {
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
                craftGrid.setCraftingSlot(slotIndex, static_cast<ItemID>(dragged.itemId),
                    static_cast<uint16_t>(dragged.count));
                m_deps.input.beginUIDragItem(static_cast<int>(current),
                    static_cast<int>(currentCount), kCraftingSlotBase + slotIndex);
            }
        }
    }

    // Return all crafting grid items to inventory (called on exit).
    void returnGridToInventory() {
        CraftingGridControl& craftGrid = m_deps.uiRenderer.getCraftingGrid();
        Inventory& inventory = m_deps.inventory;

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

private:
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
        ecs::PlayerQuery query(m_deps.ecsRegistry);
        const glm::vec3 playerPos = query.getPosition();
        const glm::ivec3 blockPos(static_cast<int>(std::floor(playerPos.x)),
                                   static_cast<int>(std::floor(playerPos.y)),
                                   static_cast<int>(std::floor(playerPos.z)));
        m_deps.dropSystem.spawnItemDrop(itemId, blockPos, count);
    }

    InventoryStateContext& m_deps;
    InventoryDragController& m_dragCtrl;
};
