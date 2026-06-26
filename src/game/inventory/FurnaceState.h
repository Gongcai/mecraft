#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <glm/vec3.hpp>

#include "../states/GameStateMachine.h"
#include "../states/IGameState.h"
#include "engine/input/InputContextManager.h"
#include "ContainerBehaviorRegistry.h"
#include "FurnaceInventoryStore.h"
#include "InventoryStateContext.h"
#include "../../crafting/SmeltingSystem.h"
#include "../../ecs/GameplayRegistry.h"
#include "../../ecs/util/PlayerQuery.h"
#include "../../item/Item.h"
#include "../../player/Inventory.h"
#include "../../ui/core/UIInputAdapter.h"
#include "../../ui/core/UIRenderer.h"
#include "../../ui/inventory/ContainerUiRegistry.h"
#include "../../world/DropSystem.h"

class FurnaceState final : public IGameState {
public:
    FurnaceState(InventoryStateContext deps,
                 std::string containerUiId,
                 std::string behaviorId,
                 const glm::ivec3 furnacePosition)
        : m_deps(deps),
          m_containerUiId(std::move(containerUiId)),
          m_behaviorId(std::move(behaviorId)),
          m_furnacePosition(furnacePosition) {}

    void onEnter() override {
        const ui::ContainerUiDef& uiDef = ui::ContainerUiRegistry::require(m_containerUiId);
        const ContainerBehaviorDef& behavior = ContainerBehaviorRegistry::require(m_behaviorId);
        configureSmeltingBehavior(uiDef, behavior);

        FurnaceInventoryStore& store = ensureStore();
        m_furnace = &store.getOrCreate(m_furnacePosition);

        m_deps.context.pushContext(InputContextType::UI);
        m_deps.input.captureMouse(false);
        m_deps.uiRenderer.setFurnacePanelDefinition(uiDef);
        m_deps.uiRenderer.setFurnacePanelSource(m_furnace);
        m_deps.uiRenderer.setFurnacePanelVisible(true);
        m_deps.uiRenderer.clearFurnacePanelActivations();
        m_lastSecondaryPlaceSlot = -1;
    }

    void onExit() override {
        returnDraggedItemToStorage();
        m_deps.uiRenderer.setFurnacePanelVisible(false);
        m_deps.uiRenderer.setFurnacePanelSource(nullptr);
        m_deps.context.popContext();
        if (m_deps.context.getCurrentContext() == InputContextType::Gameplay) {
            m_deps.input.captureMouse(true);
        }
        m_furnace = nullptr;
    }

    void update(const float dt, const InputSnapshot& snapshot) override {
        if (m_furnace == nullptr) {
            m_deps.fsm.popState();
            return;
        }

        SmeltingSystem& smelting = m_deps.ecsRegistry.ctxGet<SmeltingSystem>();
        m_furnace->tick(dt, smelting, m_processor);
        m_deps.uiRenderer.setFurnacePanelProgress(m_furnace->burnFraction(), m_furnace->cookFraction());

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
                    handleSecondaryPlace(hovered, smelting);
                } else if (uiRouteResult.secondaryPressed) {
                    returnDraggedItemToStorage();
                }
                m_deps.uiRenderer.clearFurnacePanelActivations();
                return;
            }
        }

        if (!uiRouteResult.primaryPressed ||
            uiRouteResult.primaryDown != UIEventResult::Consumed) {
            return;
        }

        handlePrimaryClick(activatedSlot(), smelting);
        m_deps.uiRenderer.clearFurnacePanelActivations();
    }

private:
    static constexpr int kFurnaceSlotBase = 30000;

    enum class SlotSpace {
        None,
        Furnace,
        Player,
    };

    struct SlotRef {
        SlotSpace space = SlotSpace::None;
        int index = -1;

        [[nodiscard]] bool valid() const {
            return space != SlotSpace::None && index >= 0;
        }
    };

    void configureSmeltingBehavior(const ui::ContainerUiDef& uiDef, const ContainerBehaviorDef& behavior) {
        if (behavior.handler != "smelting") {
            throw std::runtime_error(behavior.id + " requires smelting handler for furnace state");
        }
        if (behavior.storage.kind != ContainerStorageKind::BlockEntity) {
            throw std::runtime_error(behavior.id + " smelting handler requires block_entity storage");
        }
        if (behavior.storage.slots != FurnaceInventory::SLOT_COUNT) {
            throw std::runtime_error(behavior.id + " storage slot count must match furnace inventory storage");
        }

        const ContainerProcessorDef* smeltingProcessor = nullptr;
        for (const ContainerProcessorDef& processor : behavior.processors) {
            if (processor.type != "smelting") {
                throw std::runtime_error(behavior.id + " furnace state only supports smelting processors");
            }
            if (smeltingProcessor != nullptr) {
                throw std::runtime_error(behavior.id + " furnace state requires exactly one smelting processor");
            }
            smeltingProcessor = &processor;
        }
        if (smeltingProcessor == nullptr) {
            throw std::runtime_error(behavior.id + " furnace state requires a smelting processor");
        }

        m_processor.inputSlot = smeltingProcessor->inputSlot;
        m_processor.fuelSlot = smeltingProcessor->fuelSlot;
        m_processor.outputSlot = smeltingProcessor->outputSlot;
        validateConfiguredSlot(uiDef, m_processor.inputSlot, "input");
        validateConfiguredSlot(uiDef, m_processor.fuelSlot, "fuel");
        validateConfiguredSlot(uiDef, m_processor.outputSlot, "output");

        m_slotRules = behavior.slotRules;
        requireSlotRule(m_processor.inputSlot, "smelting_input", false, behavior.id);
        requireSlotRule(m_processor.fuelSlot, "fuel", false, behavior.id);
        requireSlotRule(m_processor.outputSlot, "any", true, behavior.id);
    }

    static void validateConfiguredSlot(const ui::ContainerUiDef& uiDef,
                                       const int slot,
                                       const char* slotName) {
        if (slot < 0 || slot >= FurnaceInventory::SLOT_COUNT) {
            throw std::runtime_error(uiDef.id + " smelting processor has invalid " + std::string(slotName) + " slot");
        }
        for (const ui::ContainerSlotGroupDef& group : uiDef.slotGroups) {
            if (group.kind != ui::ContainerSlotGroupKind::Container) {
                continue;
            }
            const int groupEnd = group.firstSlot + group.columns * group.rows;
            if (slot >= group.firstSlot && slot < groupEnd) {
                return;
            }
        }
        throw std::runtime_error(uiDef.id + " does not expose smelting processor " + std::string(slotName) + " slot");
    }

    void requireSlotRule(const int slot,
                         const std::string& accepts,
                         const bool outputOnly,
                         const std::string& behaviorId) const {
        const ContainerSlotRuleDef* rule = slotRuleFor(slot);
        if (rule == nullptr) {
            throw std::runtime_error(behaviorId + " is missing a required furnace slot rule");
        }
        if (rule->accepts != accepts || rule->outputOnly != outputOnly) {
            throw std::runtime_error(behaviorId + " furnace slot rule does not match its smelting processor role");
        }
    }

    [[nodiscard]] const ContainerSlotRuleDef* slotRuleFor(const int slot) const {
        for (const ContainerSlotRuleDef& rule : m_slotRules) {
            if (rule.slot == slot) {
                return &rule;
            }
        }
        return nullptr;
    }

    FurnaceInventoryStore& ensureStore() {
        if (!m_deps.ecsRegistry.ctxHas<FurnaceInventoryStore>()) {
            m_deps.ecsRegistry.ctxSet<FurnaceInventoryStore>();
        }
        return m_deps.ecsRegistry.ctxGet<FurnaceInventoryStore>();
    }

    [[nodiscard]] SlotRef activatedSlot() const {
        const int furnaceSlot = m_deps.uiRenderer.getFurnacePanelLastActivatedSlot();
        if (m_furnace != nullptr && m_furnace->isValidSlot(furnaceSlot)) {
            return {SlotSpace::Furnace, furnaceSlot};
        }

        const int playerGridSlot = m_deps.uiRenderer.getFurnacePanelPlayerLastActivatedSlot();
        const int inventorySlot = Inventory::toInventoryIndexFromGridSlot(playerGridSlot);
        if (m_deps.inventory.isValidSlot(inventorySlot)) {
            return {SlotSpace::Player, inventorySlot};
        }

        return {};
    }

    [[nodiscard]] SlotRef hoveredSlot() const {
        const int furnaceSlot = m_deps.uiRenderer.getFurnacePanelHoveredSlot();
        if (m_furnace != nullptr && m_furnace->isValidSlot(furnaceSlot)) {
            return {SlotSpace::Furnace, furnaceSlot};
        }

        const int playerGridSlot = m_deps.uiRenderer.getFurnacePanelPlayerHoveredSlot();
        const int inventorySlot = Inventory::toInventoryIndexFromGridSlot(playerGridSlot);
        if (m_deps.inventory.isValidSlot(inventorySlot)) {
            return {SlotSpace::Player, inventorySlot};
        }

        return {};
    }

    [[nodiscard]] static int encodeSlot(const SlotRef slot) {
        switch (slot.space) {
            case SlotSpace::Furnace:
                return kFurnaceSlotBase + slot.index;
            case SlotSpace::Player:
                return slot.index;
            case SlotSpace::None:
            default:
                return -1;
        }
    }

    [[nodiscard]] SlotRef decodeSlot(const int encodedSlot) const {
        if (encodedSlot >= kFurnaceSlotBase &&
            encodedSlot < kFurnaceSlotBase + FurnaceInventory::SLOT_COUNT) {
            return {SlotSpace::Furnace, encodedSlot - kFurnaceSlotBase};
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
        if (slot.space == SlotSpace::Furnace) {
            return m_furnace != nullptr ? m_furnace->getSlotStack(slot.index) : ItemStack{};
        }
        return m_deps.inventory.getSlotStack(slot.index);
    }

    void setSlotStack(const SlotRef slot, const ItemStack& stack) {
        if (!slot.valid()) {
            return;
        }
        if (slot.space == SlotSpace::Furnace) {
            if (m_furnace != nullptr) {
                m_furnace->setSlotStack(slot.index, stack);
            }
            return;
        }
        m_deps.inventory.setSlotStack(slot.index, stack);
    }

    [[nodiscard]] bool acceptsItem(const SlotRef slot,
                                   const ItemID itemId,
                                   const SmeltingSystem& smelting) const {
        if (!slot.valid() || itemId == 0) {
            return false;
        }
        if (slot.space == SlotSpace::Player) {
            return true;
        }
        const ContainerSlotRuleDef* rule = slotRuleFor(slot.index);
        if (rule == nullptr || rule->outputOnly) {
            return false;
        }
        if (rule->accepts == "smelting_input") {
            return smelting.findRecipe(itemId) != nullptr;
        }
        if (rule->accepts == "fuel") {
            return smelting.isFuel(itemId);
        }
        if (rule->accepts == "any") {
            return true;
        }
        throw std::runtime_error("Furnace state found an unknown slot accepts rule: " + rule->accepts);
    }

    [[nodiscard]] uint32_t addToSlot(const SlotRef slot,
                                     const ItemID itemId,
                                     const uint32_t count,
                                     const SmeltingSystem& smelting) {
        if (!slot.valid() || itemId == 0 || count == 0 || !acceptsItem(slot, itemId, smelting)) {
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

    void handlePrimaryClick(const SlotRef slot, const SmeltingSystem& smelting) {
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
        const uint32_t remaining = addToSlot(slot, draggedItemId, static_cast<uint32_t>(dragged.count), smelting);
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
        if (!target.isEmpty() && acceptsItem(slot, draggedItemId, smelting)) {
            ItemStack incoming;
            incoming.itemId = draggedItemId;
            incoming.count = static_cast<uint16_t>(std::min<uint32_t>(dragged.count, 65535u));
            setSlotStack(slot, incoming);
            m_deps.input.beginUIDragItem(static_cast<int>(target.itemId),
                                         static_cast<int>(target.count),
                                         encodeSlot(slot));
        }
    }

    void handleSecondaryPlace(const SlotRef slot, const SmeltingSystem& smelting) {
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
        const uint32_t remaining = addToSlot(slot, draggedItemId, 1, smelting);
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

        SmeltingSystem& smelting = m_deps.ecsRegistry.ctxGet<SmeltingSystem>();
        remaining = addToSlot(decodeSlot(dragged.sourceSlot), itemId, remaining, smelting);
        if (remaining > 0) {
            remaining = m_deps.inventory.addItem(itemId, remaining);
        }
        if (remaining > 0) {
            spawnItemDropAtPlayer(itemId, remaining);
        }

        m_deps.input.clearUIDragItem();
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
    glm::ivec3 m_furnacePosition{};
    FurnaceSmeltingProcessor m_processor;
    std::vector<ContainerSlotRuleDef> m_slotRules;
    FurnaceInventory* m_furnace = nullptr;
    int m_lastSecondaryPlaceSlot = -1;
};
