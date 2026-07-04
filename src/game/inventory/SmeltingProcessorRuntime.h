#pragma once

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "ContainerBehaviorRegistry.h"
#include "MachineInventoryStore.h"
#include "../../crafting/SmeltingSystem.h"
#include "../../item/Item.h"
#include "../../ui/inventory/ContainerUiRegistry.h"

class SmeltingProcessorRuntime {
public:
    [[nodiscard]] static SmeltingProcessorRuntime create(const ui::ContainerUiDef& uiDef,
                                                         const ContainerBehaviorDef& behavior,
                                                         int storageSlotCount) {
        if (behavior.handler != "smelting") {
            fail(behavior.id + " requires smelting handler");
        }
        if (behavior.storage.kind != ContainerStorageKind::BlockEntity) {
            fail(behavior.id + " smelting handler requires block_entity storage");
        }
        if (behavior.storage.slots != storageSlotCount) {
            fail(behavior.id + " storage slot count does not match smelting storage");
        }

        const ContainerProcessorDef* smeltingProcessor = nullptr;
        for (const ContainerProcessorDef& processor : behavior.processors) {
            if (processor.type != "smelting") {
                fail(behavior.id + " smelting runtime only supports smelting processors");
            }
            if (smeltingProcessor != nullptr) {
                fail(behavior.id + " smelting runtime requires exactly one processor");
            }
            smeltingProcessor = &processor;
        }
        if (smeltingProcessor == nullptr) {
            fail(behavior.id + " smelting runtime requires a processor");
        }

        SmeltingProcessorRuntime runtime;
        runtime.m_processor.inputSlot = smeltingProcessor->inputSlot;
        runtime.m_processor.fuelSlot = smeltingProcessor->fuelSlot;
        runtime.m_processor.outputSlot = smeltingProcessor->outputSlot;
        runtime.m_storageSlotCount = storageSlotCount;
        runtime.m_slotRules = behavior.slotRules;

        validateConfiguredSlot(uiDef, storageSlotCount, runtime.m_processor.inputSlot, "input");
        validateConfiguredSlot(uiDef, storageSlotCount, runtime.m_processor.fuelSlot, "fuel");
        validateConfiguredSlot(uiDef, storageSlotCount, runtime.m_processor.outputSlot, "output");

        runtime.requireSlotRule(runtime.m_processor.inputSlot, "smelting_input", false, behavior.id);
        runtime.requireSlotRule(runtime.m_processor.fuelSlot, "fuel", false, behavior.id);
        runtime.requireSlotRule(runtime.m_processor.outputSlot, "any", true, behavior.id);
        return runtime;
    }

    [[nodiscard]] const MachineSmeltingProcessor& processor() const {
        return m_processor;
    }

    [[nodiscard]] bool acceptsItem(const int slot, const ItemID itemId, const SmeltingSystem& smelting) const {
        if (slot < 0 || slot >= m_storageSlotCount || itemId == 0) {
            return false;
        }

        const ContainerSlotRuleDef* rule = slotRuleFor(slot);
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
        fail("Smelting runtime found an unknown slot accepts rule: " + rule->accepts);
    }

private:
    [[noreturn]] static void fail(const std::string& message) {
        std::cerr << message << '\n';
        std::abort();
    }

    [[nodiscard]] static bool uiExposesSlot(const ui::ContainerUiDef& uiDef, const int slot) {
        for (const ui::ContainerSlotGroupDef& group : uiDef.slotGroups) {
            if (group.kind != ui::ContainerSlotGroupKind::Container) {
                continue;
            }
            const int groupEnd = group.firstSlot + group.columns * group.rows;
            if (slot >= group.firstSlot && slot < groupEnd) {
                return true;
            }
        }
        return false;
    }

    static void validateConfiguredSlot(const ui::ContainerUiDef& uiDef,
                                       const int storageSlotCount,
                                       const int slot,
        const char* slotName) {
        if (slot < 0 || slot >= storageSlotCount) {
            fail(uiDef.id + " smelting processor has invalid " + std::string(slotName) + " slot");
        }
        if (!uiExposesSlot(uiDef, slot)) {
            fail(uiDef.id + " does not expose smelting processor " + std::string(slotName) + " slot");
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

    void requireSlotRule(const int slot,
                         const std::string& accepts,
                         const bool outputOnly,
                         const std::string& behaviorId) const {
        const ContainerSlotRuleDef* rule = slotRuleFor(slot);
        if (rule == nullptr) {
            fail(behaviorId + " is missing a required smelting slot rule");
        }
        if (rule->accepts != accepts || rule->outputOnly != outputOnly) {
            fail(behaviorId + " smelting slot rule does not match its processor role");
        }
    }

    MachineSmeltingProcessor m_processor;
    int m_storageSlotCount = 0;
    std::vector<ContainerSlotRuleDef> m_slotRules;
};
