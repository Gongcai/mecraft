#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <unordered_map>

#include <glm/vec3.hpp>

#include "../../crafting/SmeltingSystem.h"
#include "../../item/Item.h"

struct FurnaceSmeltingProcessor {
    int inputSlot = 0;
    int fuelSlot = 1;
    int outputSlot = 2;
};

class FurnaceInventory {
public:
    static constexpr int INPUT_SLOT = 0;
    static constexpr int FUEL_SLOT = 1;
    static constexpr int OUTPUT_SLOT = 2;
    static constexpr int SLOT_COUNT = 3;

    [[nodiscard]] bool isValidSlot(const int slot) const {
        return slot >= 0 && slot < SLOT_COUNT;
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

    void tick(float dt, const SmeltingSystem& smeltingSystem, const FurnaceSmeltingProcessor& processor) {
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
    static void validateProcessor(const FurnaceSmeltingProcessor& processor) {
        if (processor.inputSlot < 0 ||
            processor.inputSlot >= SLOT_COUNT ||
            processor.fuelSlot < 0 ||
            processor.fuelSlot >= SLOT_COUNT ||
            processor.outputSlot < 0 ||
            processor.outputSlot >= SLOT_COUNT) {
            throw std::runtime_error("Furnace smelting processor references a slot outside furnace storage");
        }
        if (processor.inputSlot == processor.fuelSlot ||
            processor.inputSlot == processor.outputSlot ||
            processor.fuelSlot == processor.outputSlot) {
            throw std::runtime_error("Furnace smelting processor requires distinct input, fuel, and output slots");
        }
    }

    [[nodiscard]] const SmeltingRecipe* currentRecipe(const SmeltingSystem& smeltingSystem,
                                                      const FurnaceSmeltingProcessor& processor) const {
        const ItemStack input = m_slots[static_cast<std::size_t>(processor.inputSlot)];
        if (input.isEmpty()) {
            return nullptr;
        }
        return smeltingSystem.findRecipe(input.itemId);
    }

    [[nodiscard]] bool canReceiveResult(const SmeltingRecipe& recipe,
                                        const FurnaceSmeltingProcessor& processor) const {
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
                                   const FurnaceSmeltingProcessor& processor) {
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

    void completeRecipe(const SmeltingRecipe& recipe, const FurnaceSmeltingProcessor& processor) {
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

    std::array<ItemStack, SLOT_COUNT> m_slots{};
    float m_burnSecondsRemaining = 0.0f;
    float m_burnSecondsTotal = 0.0f;
    float m_cookSeconds = 0.0f;
    float m_cookTargetSeconds = 0.0f;
};

class FurnaceInventoryStore {
public:
    [[nodiscard]] FurnaceInventory& getOrCreate(const glm::ivec3& position) {
        return m_furnaces[positionKey(position)];
    }

    [[nodiscard]] const FurnaceInventory* find(const glm::ivec3& position) const {
        const auto it = m_furnaces.find(positionKey(position));
        if (it == m_furnaces.end()) {
            return nullptr;
        }
        return &it->second;
    }

    [[nodiscard]] bool empty() const {
        return m_furnaces.empty();
    }

    template <typename Fn>
    void forEach(Fn&& fn) const {
        for (const auto& [key, furnace] : m_furnaces) {
            fn(glm::ivec3(key.x, key.y, key.z), furnace);
        }
    }

    [[nodiscard]] std::array<ItemStack, FurnaceInventory::SLOT_COUNT> extractAndErase(const glm::ivec3& position) {
        std::array<ItemStack, FurnaceInventory::SLOT_COUNT> contents{};
        const auto it = m_furnaces.find(positionKey(position));
        if (it == m_furnaces.end()) {
            return contents;
        }

        for (int slot = 0; slot < FurnaceInventory::SLOT_COUNT; ++slot) {
            contents[static_cast<std::size_t>(slot)] = it->second.getSlotStack(slot);
        }
        m_furnaces.erase(it);
        return contents;
    }

    void erase(const glm::ivec3& position) {
        m_furnaces.erase(positionKey(position));
    }

private:
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

    [[nodiscard]] static Key positionKey(const glm::ivec3& position) {
        return {position.x, position.y, position.z};
    }

    std::unordered_map<Key, FurnaceInventory, KeyHash> m_furnaces;
};
