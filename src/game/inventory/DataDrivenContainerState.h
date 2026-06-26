#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

#include <glm/vec3.hpp>

#include "../states/GameStateMachine.h"
#include "../states/IGameState.h"
#include "engine/input/InputContextManager.h"
#include "ChestInventoryStore.h"
#include "ContainerBehaviorRegistry.h"
#include "InventoryStateContext.h"
#include "../../ecs/GameplayRegistry.h"
#include "../../ecs/util/PlayerQuery.h"
#include "../../item/Item.h"
#include "../../player/Inventory.h"
#include "../../ui/core/UIInputAdapter.h"
#include "../../ui/core/UIRenderer.h"
#include "../../ui/inventory/ContainerUiRegistry.h"
#include "../../world/DropSystem.h"

class DataDrivenContainerState final : public IGameState {
public:
    DataDrivenContainerState(InventoryStateContext deps,
                             std::string containerUiId,
                             std::string behaviorId,
                             const glm::ivec3 blockPosition)
        : m_deps(deps),
          m_containerUiId(std::move(containerUiId)),
          m_behaviorId(std::move(behaviorId)),
          m_blockPosition(blockPosition) {}

    void onEnter() override {
        const ui::ContainerUiDef& uiDef = ui::ContainerUiRegistry::require(m_containerUiId);
        const ContainerBehaviorDef& behavior = ContainerBehaviorRegistry::require(m_behaviorId);
        validateStorageBehavior(uiDef, behavior);
        m_storageSlotCount = behavior.storage.slots;

        ChestInventoryStore& store = ensureStore();
        m_storage = &store.getOrCreate(m_blockPosition);

        m_deps.context.pushContext(InputContextType::UI);
        m_deps.input.captureMouse(false);
        m_deps.uiRenderer.setChestPanelDefinition(uiDef);
        m_deps.uiRenderer.setChestPanelChestSource(m_storage);
        m_deps.uiRenderer.setChestPanelVisible(true);
        m_deps.uiRenderer.clearChestPanelActivations();
        m_lastSecondaryPlaceSlot = -1;
    }

    void onExit() override {
        returnDraggedItemToStorage();
        m_deps.uiRenderer.setChestPanelVisible(false);
        m_deps.uiRenderer.setChestPanelChestSource(nullptr);
        m_deps.context.popContext();
        if (m_deps.context.getCurrentContext() == InputContextType::Gameplay) {
            m_deps.input.captureMouse(true);
        }
        m_storage = nullptr;
        m_storageSlotCount = 0;
    }

    void update(float /*dt*/, const InputSnapshot& snapshot) override {
        if (m_storage == nullptr) {
            m_deps.fsm.popState();
            return;
        }

        const UIInputRouteResult uiRouteResult =
            UIInputAdapter::routeInput(m_deps.uiRenderer, snapshot, m_deps.context);

        if (m_deps.context.isActionTriggered(Action::Inventory) ||
            m_deps.context.isActionTriggered(Action::Menu) ||
            (m_deps.context.isActionTriggered(Action::Cancel) &&
             uiRouteResult.aggregate != UIEventResult::Consumed)) {
            m_deps.fsm.popState();
            return;
        }

        const bool secondaryHeld = m_deps.context.isActionHeld(Action::UISecondaryClick);
        if (!secondaryHeld) {
            m_lastSecondaryPlaceSlot = -1;
        }

        if (uiRouteResult.secondaryPressed || secondaryHeld) {
            const auto& dragged = m_deps.input.getUIDragItem();
            if (dragged.active && dragged.itemId > 0 && dragged.count > 0) {
                const SlotRef hovered = hoveredSlot();
                if (hovered.valid()) {
                    handleSecondaryPlace(hovered);
                } else if (uiRouteResult.secondaryPressed) {
                    returnDraggedItemToStorage();
                }
                m_deps.uiRenderer.clearChestPanelActivations();
                return;
            }
        }

        if (!uiRouteResult.primaryPressed ||
            uiRouteResult.primaryDown != UIEventResult::Consumed) {
            return;
        }

        handlePrimaryClick(activatedSlot());
        m_deps.uiRenderer.clearChestPanelActivations();
    }

private:
    static constexpr int kContainerSlotBase = 20000;

    enum class SlotSpace {
        None,
        Container,
        Player,
    };

    struct SlotRef {
        SlotSpace space = SlotSpace::None;
        int index = -1;

        [[nodiscard]] bool valid() const {
            return space != SlotSpace::None && index >= 0;
        }
    };

    static void validateStorageBehavior(const ui::ContainerUiDef& uiDef,
                                        const ContainerBehaviorDef& behavior) {
        if (behavior.handler != "storage") {
            throw std::runtime_error(behavior.id + " requires storage handler for data-driven storage state");
        }
        if (behavior.storage.kind != ContainerStorageKind::BlockEntity) {
            throw std::runtime_error(behavior.id + " requires block_entity storage");
        }
        if (!behavior.processors.empty()) {
            throw std::runtime_error(behavior.id + " storage behavior must not declare processors");
        }
        if (behavior.storage.slots > ChestInventory::SLOT_COUNT) {
            throw std::runtime_error(behavior.id + " declares more storage slots than the backing store supports");
        }

        bool hasContainerSlots = false;
        for (const ui::ContainerSlotGroupDef& group : uiDef.slotGroups) {
            if (group.kind != ui::ContainerSlotGroupKind::Container) {
                continue;
            }
            hasContainerSlots = true;
            const int groupEnd = group.firstSlot + group.columns * group.rows;
            if (groupEnd > behavior.storage.slots) {
                throw std::runtime_error(uiDef.id + " container slot group exceeds behavior storage slots: " + group.id);
            }
        }
        if (!hasContainerSlots) {
            throw std::runtime_error(uiDef.id + " requires at least one container slot group");
        }
    }

    [[nodiscard]] bool isContainerSlotValid(const int slot) const {
        return m_storage != nullptr &&
               slot >= 0 &&
               slot < m_storageSlotCount &&
               m_storage->isValidSlot(slot);
    }

    ChestInventoryStore& ensureStore() {
        if (!m_deps.ecsRegistry.ctxHas<ChestInventoryStore>()) {
            m_deps.ecsRegistry.ctxSet<ChestInventoryStore>();
        }
        return m_deps.ecsRegistry.ctxGet<ChestInventoryStore>();
    }

    [[nodiscard]] SlotRef activatedSlot() const {
        const int containerSlot = m_deps.uiRenderer.getChestPanelLastActivatedSlot();
        if (isContainerSlotValid(containerSlot)) {
            return {SlotSpace::Container, containerSlot};
        }

        const int playerGridSlot = m_deps.uiRenderer.getChestPanelPlayerLastActivatedSlot();
        const int inventorySlot = Inventory::toInventoryIndexFromGridSlot(playerGridSlot);
        if (m_deps.inventory.isValidSlot(inventorySlot)) {
            return {SlotSpace::Player, inventorySlot};
        }

        return {};
    }

    [[nodiscard]] SlotRef hoveredSlot() const {
        const int containerSlot = m_deps.uiRenderer.getChestPanelHoveredSlot();
        if (isContainerSlotValid(containerSlot)) {
            return {SlotSpace::Container, containerSlot};
        }

        const int playerGridSlot = m_deps.uiRenderer.getChestPanelPlayerHoveredSlot();
        const int inventorySlot = Inventory::toInventoryIndexFromGridSlot(playerGridSlot);
        if (m_deps.inventory.isValidSlot(inventorySlot)) {
            return {SlotSpace::Player, inventorySlot};
        }

        return {};
    }

    [[nodiscard]] static int encodeSlot(const SlotRef slot) {
        switch (slot.space) {
            case SlotSpace::Container:
                return kContainerSlotBase + slot.index;
            case SlotSpace::Player:
                return slot.index;
            case SlotSpace::None:
            default:
                return -1;
        }
    }

    [[nodiscard]] SlotRef decodeSlot(const int encodedSlot) const {
        if (encodedSlot >= kContainerSlotBase &&
            encodedSlot < kContainerSlotBase + m_storageSlotCount) {
            return {SlotSpace::Container, encodedSlot - kContainerSlotBase};
        }
        if (m_deps.inventory.isValidSlot(encodedSlot)) {
            return {SlotSpace::Player, encodedSlot};
        }
        return {};
    }

    [[nodiscard]] ItemStack getSlotStack(const SlotRef slot) const {
        if (!slot.valid()) {
            return {};
        }
        if (slot.space == SlotSpace::Container) {
            return isContainerSlotValid(slot.index) ? m_storage->getSlotStack(slot.index) : ItemStack{};
        }
        return m_deps.inventory.getSlotStack(slot.index);
    }

    void setSlotStack(const SlotRef slot, const ItemStack& stack) {
        if (!slot.valid()) {
            return;
        }
        if (slot.space == SlotSpace::Container) {
            if (isContainerSlotValid(slot.index)) {
                m_storage->setSlotStack(slot.index, stack);
            }
            return;
        }
        m_deps.inventory.setSlotStack(slot.index, stack);
    }

    [[nodiscard]] uint32_t addToSlot(const SlotRef slot,
                                     const ItemID itemId,
                                     const uint32_t count) {
        if (!slot.valid() || itemId == 0 || count == 0) {
            return count;
        }

        const ItemDef& def = ItemRegistry::get(itemId);
        if (def.maxStack == 0) {
            return count;
        }

        ItemStack target = getSlotStack(slot);
        if (target.isEmpty()) {
            const uint16_t toAdd = static_cast<uint16_t>(std::min<uint32_t>(count, def.maxStack));
            ItemStack newStack;
            newStack.itemId = itemId;
            newStack.count = toAdd;
            newStack.durability = def.isTool ? def.maxDurability : 0;
            setSlotStack(slot, newStack);
            return count - toAdd;
        }

        if (target.itemId != itemId || target.count >= def.maxStack) {
            return count;
        }

        const uint16_t freeSpace = static_cast<uint16_t>(def.maxStack - target.count);
        const uint16_t toAdd = static_cast<uint16_t>(std::min<uint32_t>(count, freeSpace));
        target.count = static_cast<uint16_t>(target.count + toAdd);
        setSlotStack(slot, target);
        return count - toAdd;
    }

    void handlePrimaryClick(const SlotRef slot) {
        if (!slot.valid()) {
            return;
        }

        const auto& dragged = m_deps.input.getUIDragItem();
        if (!dragged.active) {
            const ItemStack picked = getSlotStack(slot);
            if (!picked.isEmpty()) {
                setSlotStack(slot, {});
                m_deps.input.beginUIDragItem(static_cast<int>(picked.itemId),
                                             static_cast<int>(picked.count),
                                             encodeSlot(slot));
            }
            return;
        }

        const ItemID draggedItemId = static_cast<ItemID>(dragged.itemId);
        const uint32_t remaining = addToSlot(slot, draggedItemId, static_cast<uint32_t>(dragged.count));
        if (remaining == 0) {
            m_deps.input.clearUIDragItem();
            return;
        }

        const ItemStack target = getSlotStack(slot);
        if (target.itemId == draggedItemId) {
            m_deps.input.beginUIDragItem(dragged.itemId,
                                         static_cast<int>(remaining),
                                         dragged.sourceSlot);
            return;
        }

        ItemStack incoming;
        incoming.itemId = draggedItemId;
        incoming.count = static_cast<uint16_t>(std::min<uint32_t>(dragged.count, 65535u));
        setSlotStack(slot, incoming);

        if (!target.isEmpty()) {
            m_deps.input.beginUIDragItem(static_cast<int>(target.itemId),
                                         static_cast<int>(target.count),
                                         encodeSlot(slot));
        } else {
            m_deps.input.clearUIDragItem();
        }
    }

    void handleSecondaryPlace(const SlotRef slot) {
        const int encoded = encodeSlot(slot);
        if (encoded == m_lastSecondaryPlaceSlot) {
            return;
        }
        m_lastSecondaryPlaceSlot = encoded;

        const auto& dragged = m_deps.input.getUIDragItem();
        if (!dragged.active || dragged.itemId <= 0 || dragged.count <= 0) {
            return;
        }

        const ItemID draggedItemId = static_cast<ItemID>(dragged.itemId);
        const uint32_t remaining = addToSlot(slot, draggedItemId, 1);
        if (remaining != 0) {
            return;
        }

        const int newCount = dragged.count - 1;
        if (newCount <= 0) {
            m_deps.input.clearUIDragItem();
        } else {
            m_deps.input.beginUIDragItem(dragged.itemId, newCount, dragged.sourceSlot);
        }
    }

    void returnDraggedItemToStorage() {
        const auto& dragged = m_deps.input.getUIDragItem();
        if (!dragged.active || dragged.itemId <= 0 || dragged.count <= 0) {
            return;
        }

        const ItemID itemId = static_cast<ItemID>(dragged.itemId);
        uint32_t remaining = static_cast<uint32_t>(dragged.count);

        remaining = addToSlot(decodeSlot(dragged.sourceSlot), itemId, remaining);
        if (remaining > 0) {
            remaining = m_deps.inventory.addItem(itemId, remaining);
        }
        if (remaining > 0 && m_storage != nullptr) {
            remaining = addToStorage(itemId, remaining);
        }
        if (remaining > 0) {
            spawnItemDropAtPlayer(itemId, remaining);
        }

        m_deps.input.clearUIDragItem();
    }

    [[nodiscard]] uint32_t addToStorage(const ItemID itemId, uint32_t count) {
        if (m_storage == nullptr || itemId == 0 || count == 0) {
            return count;
        }

        const ItemDef& def = ItemRegistry::get(itemId);
        if (def.maxStack == 0) {
            return count;
        }

        for (int slot = 0; slot < m_storageSlotCount; ++slot) {
            ItemStack stack = m_storage->getSlotStack(slot);
            if (count == 0) {
                return 0;
            }
            if (stack.isEmpty() || stack.itemId != itemId || stack.count >= def.maxStack) {
                continue;
            }

            const uint16_t freeSpace = static_cast<uint16_t>(def.maxStack - stack.count);
            const uint16_t add = static_cast<uint16_t>(std::min<uint32_t>(count, freeSpace));
            stack.count = static_cast<uint16_t>(stack.count + add);
            m_storage->setSlotStack(slot, stack);
            count -= add;
        }

        for (int slot = 0; slot < m_storageSlotCount; ++slot) {
            if (count == 0) {
                return 0;
            }
            if (!m_storage->getSlotStack(slot).isEmpty()) {
                continue;
            }

            const uint16_t add = static_cast<uint16_t>(std::min<uint32_t>(count, def.maxStack));
            ItemStack stack;
            stack.itemId = itemId;
            stack.count = add;
            stack.durability = def.isTool ? def.maxDurability : 0;
            m_storage->setSlotStack(slot, stack);
            count -= add;
        }

        return count;
    }

    void spawnItemDropAtPlayer(const ItemID itemId, const uint32_t count) {
        ecs::PlayerQuery query(m_deps.ecsRegistry);
        const glm::vec3 playerPos = query.getPosition();
        const glm::ivec3 blockPos(static_cast<int>(std::floor(playerPos.x)),
                                  static_cast<int>(std::floor(playerPos.y)),
                                  static_cast<int>(std::floor(playerPos.z)));
        m_deps.dropSystem.spawnItemDrop(itemId, blockPos, count);
    }

    InventoryStateContext m_deps;
    std::string m_containerUiId;
    std::string m_behaviorId;
    glm::ivec3 m_blockPosition{};
    ChestInventory* m_storage = nullptr;
    int m_storageSlotCount = 0;
    int m_lastSecondaryPlaceSlot = -1;
};
