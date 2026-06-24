#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>

#include <glm/vec3.hpp>

#include "../../crafting/SmeltingSystem.h"
#include "../../item/Item.h"

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

    void tick(float dt, const SmeltingSystem& smeltingSystem) {
        if (dt <= 0.0f) {
            return;
        }

        if (m_burnSecondsRemaining > 0.0f) {
            m_burnSecondsRemaining = std::max(0.0f, m_burnSecondsRemaining - dt);
        }

        const SmeltingRecipe* recipe = currentRecipe(smeltingSystem);
        if (recipe == nullptr || !canReceiveResult(*recipe)) {
            m_cookSeconds = 0.0f;
            m_cookTargetSeconds = recipe ? recipe->cookSeconds : 0.0f;
            return;
        }

        m_cookTargetSeconds = recipe->cookSeconds;
        if (m_burnSecondsRemaining <= 0.0f && !consumeFuel(smeltingSystem)) {
            return;
        }

        m_cookSeconds += dt;
        while (m_cookSeconds >= m_cookTargetSeconds && currentRecipe(smeltingSystem) != nullptr) {
            const SmeltingRecipe* activeRecipe = currentRecipe(smeltingSystem);
            if (activeRecipe == nullptr || !canReceiveResult(*activeRecipe)) {
                break;
            }
            completeRecipe(*activeRecipe);
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

private:
    [[nodiscard]] const SmeltingRecipe* currentRecipe(const SmeltingSystem& smeltingSystem) const {
        const ItemStack input = m_slots[INPUT_SLOT];
        if (input.isEmpty()) {
            return nullptr;
        }
        return smeltingSystem.findRecipe(input.itemId);
    }

    [[nodiscard]] bool canReceiveResult(const SmeltingRecipe& recipe) const {
        const ItemDef& resultDef = ItemRegistry::get(recipe.result);
        if (resultDef.maxStack == 0) {
            return false;
        }

        const ItemStack output = m_slots[OUTPUT_SLOT];
        if (output.isEmpty()) {
            return recipe.resultCount <= resultDef.maxStack;
        }
        if (output.itemId != recipe.result) {
            return false;
        }
        return static_cast<uint32_t>(output.count) + recipe.resultCount <= resultDef.maxStack;
    }

    [[nodiscard]] bool consumeFuel(const SmeltingSystem& smeltingSystem) {
        ItemStack& fuel = m_slots[FUEL_SLOT];
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

    void completeRecipe(const SmeltingRecipe& recipe) {
        ItemStack& input = m_slots[INPUT_SLOT];
        --input.count;
        if (input.count == 0) {
            input = {};
        }

        ItemStack& output = m_slots[OUTPUT_SLOT];
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
