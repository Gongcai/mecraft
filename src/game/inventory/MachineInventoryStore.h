#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <unordered_map>

#include <glm/vec3.hpp>

#include "../../crafting/SmeltingSystem.h"
#include "../../item/Item.h"
#include "BlockEntityInventoryStore.h"

struct MachineSmeltingProcessor {
    int inputSlot = 0;
    int fuelSlot = 1;
    int outputSlot = 2;
};

class MachineInventory {
public:
    static constexpr int DEFAULT_SMELTING_INPUT_SLOT = 0;
    static constexpr int DEFAULT_SMELTING_FUEL_SLOT = 1;
    static constexpr int DEFAULT_SMELTING_OUTPUT_SLOT = 2;
    static constexpr int MAX_SLOT_COUNT = BlockEntityInventory::SLOT_COUNT;

    explicit MachineInventory(const int slotCount = 0)
        : m_slotCount(slotCount) {
        validateSlotCount(slotCount);
    }

    [[nodiscard]] int slotCount() const {
        return m_slotCount;
    }

    [[nodiscard]] bool isValidSlot(const int slot) const {
        return slot >= 0 && slot < m_slotCount;
    }

    [[nodiscard]] ItemStack getSlotStack(const int slot) const {
        if (!isValidSlot(slot)) {
            return {};
        }
        return m_slots[static_cast<std::size_t>(slot)];
    }

    void setSlotStack(const int slot, const ItemStack& stack) {
        if (!isValidSlot(slot)) {
            return;
        }
        m_slots[static_cast<std::size_t>(slot)] = stack.isEmpty() ? ItemStack{} : stack;
    }

    void tick(float dt, const SmeltingSystem& smeltingSystem, const MachineSmeltingProcessor& processor) {
        if (dt <= 0.0f) {
            return;
        }

        if (m_burnSecondsRemaining > 0.0f) {
            m_burnSecondsRemaining = std::max(0.0f, m_burnSecondsRemaining - dt);
        }

        validateProcessor(processor);

        const SmeltingRecipe* recipe = currentRecipe(smeltingSystem, processor);
        if (recipe == nullptr || !canReceiveResult(*recipe, processor)) {
            m_cookSeconds = 0.0f;
            m_cookTargetSeconds = recipe ? recipe->cookSeconds : 0.0f;
            return;
        }

        m_cookTargetSeconds = recipe->cookSeconds;
        if (m_burnSecondsRemaining <= 0.0f && !consumeFuel(smeltingSystem, processor)) {
            return;
        }

        m_cookSeconds += dt;
        while (m_cookSeconds >= m_cookTargetSeconds && currentRecipe(smeltingSystem, processor) != nullptr) {
            const SmeltingRecipe* activeRecipe = currentRecipe(smeltingSystem, processor);
            if (activeRecipe == nullptr || !canReceiveResult(*activeRecipe, processor)) {
                break;
            }
            completeRecipe(*activeRecipe, processor);
            m_cookSeconds -= m_cookTargetSeconds;
            m_cookTargetSeconds = activeRecipe->cookSeconds;
        }
    }

    [[nodiscard]] float burnFraction() const {
        if (m_burnSecondsTotal <= 0.0f) {
            return 0.0f;
        }
        return std::clamp(m_burnSecondsRemaining / m_burnSecondsTotal, 0.0f, 1.0f);
    }

    [[nodiscard]] float cookFraction() const {
        if (m_cookTargetSeconds <= 0.0f) {
            return 0.0f;
        }
        return std::clamp(m_cookSeconds / m_cookTargetSeconds, 0.0f, 1.0f);
    }

    [[nodiscard]] float burnSecondsRemaining() const {
        return m_burnSecondsRemaining;
    }

    [[nodiscard]] float burnSecondsTotal() const {
        return m_burnSecondsTotal;
    }

    [[nodiscard]] float cookSeconds() const {
        return m_cookSeconds;
    }

    [[nodiscard]] float cookTargetSeconds() const {
        return m_cookTargetSeconds;
    }

    void setProgress(const float burnSecondsRemaining,
                     const float burnSecondsTotal,
                     const float cookSeconds,
                     const float cookTargetSeconds) {
        m_burnSecondsRemaining = std::max(0.0f, burnSecondsRemaining);
        m_burnSecondsTotal = std::max(0.0f, burnSecondsTotal);
        m_cookSeconds = std::max(0.0f, cookSeconds);
        m_cookTargetSeconds = std::max(0.0f, cookTargetSeconds);
        if (m_burnSecondsRemaining > m_burnSecondsTotal) {
            m_burnSecondsRemaining = m_burnSecondsTotal;
        }
    }

private:
    friend class MachineInventoryStore;

    [[noreturn]] static void fail(const char* message) {
        std::cerr << message << '\n';
        std::abort();
    }

    static void validateSlotCount(const int slotCount) {
        if (slotCount < 0 || slotCount > MAX_SLOT_COUNT) {
            fail("Machine inventory slot count is outside the supported range");
        }
    }

    void configureSlotCount(const int slotCount) {
        validateSlotCount(slotCount);
        if (m_slotCount != 0 && m_slotCount != slotCount) {
            fail("Machine inventory slot count cannot change after creation");
        }
        m_slotCount = slotCount;
    }

    void validateProcessor(const MachineSmeltingProcessor& processor) const {
        if (processor.inputSlot < 0 ||
            processor.inputSlot >= m_slotCount ||
            processor.fuelSlot < 0 ||
            processor.fuelSlot >= m_slotCount ||
            processor.outputSlot < 0 ||
            processor.outputSlot >= m_slotCount) {
            fail("Machine smelting processor references a slot outside machine storage");
        }
        if (processor.inputSlot == processor.fuelSlot ||
            processor.inputSlot == processor.outputSlot ||
            processor.fuelSlot == processor.outputSlot) {
            fail("Machine smelting processor requires distinct input, fuel, and output slots");
        }
    }

    [[nodiscard]] const SmeltingRecipe* currentRecipe(const SmeltingSystem& smeltingSystem,
                                                      const MachineSmeltingProcessor& processor) const {
        const ItemStack input = m_slots[static_cast<std::size_t>(processor.inputSlot)];
        if (input.isEmpty()) {
            return nullptr;
        }
        return smeltingSystem.findRecipe(input.itemId);
    }

    [[nodiscard]] bool canReceiveResult(const SmeltingRecipe& recipe,
                                        const MachineSmeltingProcessor& processor) const {
        const ItemDef& resultDef = ItemRegistry::get(recipe.result);
        if (resultDef.maxStack == 0) {
            return false;
        }

        const ItemStack output = m_slots[static_cast<std::size_t>(processor.outputSlot)];
        if (output.isEmpty()) {
            return recipe.resultCount <= resultDef.maxStack;
        }
        if (output.itemId != recipe.result) {
            return false;
        }
        return static_cast<uint32_t>(output.count) + recipe.resultCount <= resultDef.maxStack;
    }

    [[nodiscard]] bool consumeFuel(const SmeltingSystem& smeltingSystem,
                                   const MachineSmeltingProcessor& processor) {
        ItemStack& fuel = m_slots[static_cast<std::size_t>(processor.fuelSlot)];
        if (fuel.isEmpty()) {
            return false;
        }

        const float seconds = smeltingSystem.fuelSeconds(fuel.itemId);
        if (seconds <= 0.0f) {
            return false;
        }

        --fuel.count;
        if (fuel.count == 0) {
            fuel = {};
        }
        m_burnSecondsRemaining = seconds;
        m_burnSecondsTotal = seconds;
        return true;
    }

    void completeRecipe(const SmeltingRecipe& recipe, const MachineSmeltingProcessor& processor) {
        ItemStack& input = m_slots[static_cast<std::size_t>(processor.inputSlot)];
        --input.count;
        if (input.count == 0) {
            input = {};
        }

        ItemStack& output = m_slots[static_cast<std::size_t>(processor.outputSlot)];
        if (output.isEmpty()) {
            output.itemId = recipe.result;
            output.count = recipe.resultCount;
            output.durability = 0;
            return;
        }
        output.count = static_cast<uint16_t>(output.count + recipe.resultCount);
    }

    int m_slotCount = 0;
    std::array<ItemStack, MAX_SLOT_COUNT> m_slots{};
    float m_burnSecondsRemaining = 0.0f;
    float m_burnSecondsTotal = 0.0f;
    float m_cookSeconds = 0.0f;
    float m_cookTargetSeconds = 0.0f;
};

class MachineInventoryStore {
public:
    [[nodiscard]] MachineInventory& getOrCreate(const glm::ivec3& position,
                                                const std::string& typeId,
                                                const int slotCount) {
        validateTypeAndSlotCount(typeId, slotCount);
        Entry& entry = m_entries[positionKey(position)];
        if (entry.typeId.empty()) {
            entry.typeId = typeId;
            entry.slotCount = slotCount;
            entry.inventory.configureSlotCount(slotCount);
            return entry.inventory;
        }
        if (entry.typeId != typeId || entry.slotCount != slotCount) {
            fail("Machine inventory metadata mismatch at position");
        }
        return entry.inventory;
    }

    [[nodiscard]] const MachineInventory* find(const glm::ivec3& position) const {
        const auto it = m_entries.find(positionKey(position));
        if (it == m_entries.end()) {
            return nullptr;
        }
        return &it->second.inventory;
    }

    [[nodiscard]] MachineInventory* findMutable(const glm::ivec3& position) {
        const auto it = m_entries.find(positionKey(position));
        if (it == m_entries.end()) {
            return nullptr;
        }
        return &it->second.inventory;
    }

    [[nodiscard]] bool empty() const {
        return m_entries.empty();
    }

    template <typename Fn>
    void forEach(Fn&& fn) const {
        for (const auto& [key, entry] : m_entries) {
            fn(glm::ivec3(key.x, key.y, key.z), entry.typeId, entry.inventory);
        }
    }

    [[nodiscard]] std::array<ItemStack, MachineInventory::MAX_SLOT_COUNT> extractAndErase(
        const glm::ivec3& position) {
        std::array<ItemStack, MachineInventory::MAX_SLOT_COUNT> contents{};
        const auto it = m_entries.find(positionKey(position));
        if (it == m_entries.end()) {
            return contents;
        }

        for (int slot = 0; slot < it->second.slotCount; ++slot) {
            contents[static_cast<std::size_t>(slot)] = it->second.inventory.getSlotStack(slot);
        }
        m_entries.erase(it);
        return contents;
    }

    void erase(const glm::ivec3& position) {
        m_entries.erase(positionKey(position));
    }

private:
    [[noreturn]] static void fail(const char* message) {
        std::cerr << message << '\n';
        std::abort();
    }

    struct Key {
        int x = 0;
        int y = 0;
        int z = 0;

        [[nodiscard]] bool operator==(const Key& other) const {
            return x == other.x && y == other.y && z == other.z;
        }
    };

    struct KeyHash {
        [[nodiscard]] std::size_t operator()(const Key& key) const {
            const std::size_t hx = std::hash<int>{}(key.x);
            const std::size_t hy = std::hash<int>{}(key.y);
            const std::size_t hz = std::hash<int>{}(key.z);
            return hx ^ (hy << 1u) ^ (hz << 2u);
        }
    };

    struct Entry {
        std::string typeId;
        int slotCount = 0;
        MachineInventory inventory;
    };

    static void validateTypeAndSlotCount(const std::string& typeId, const int slotCount) {
        if (typeId.empty()) {
            fail("Machine inventory requires a type id");
        }
        if (slotCount <= 0 || slotCount > MachineInventory::MAX_SLOT_COUNT) {
            fail("Machine inventory slot count is outside the supported range");
        }
    }

    [[nodiscard]] static Key positionKey(const glm::ivec3& position) {
        return {position.x, position.y, position.z};
    }

    std::unordered_map<Key, Entry, KeyHash> m_entries;
};
