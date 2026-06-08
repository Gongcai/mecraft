#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>

#include <glm/vec3.hpp>

#include "../../item/Item.h"

class ChestInventory {
public:
    static constexpr int COLUMNS = 9;
    static constexpr int ROWS = 6;
    static constexpr int SLOT_COUNT = COLUMNS * ROWS;

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

    void setSlotItem(const int slot, const ItemID itemId, const uint16_t count = 1) {
        if (!isValidSlot(slot)) {
            return;
        }
        if (itemId == 0 || count == 0) {
            m_slots[static_cast<std::size_t>(slot)] = {};
            return;
        }

        ItemStack stack;
        stack.itemId = itemId;
        stack.count = count;
        m_slots[static_cast<std::size_t>(slot)] = stack;
    }

    [[nodiscard]] uint32_t addItem(const ItemID itemId, uint32_t count) {
        if (itemId == 0 || count == 0) {
            return count;
        }

        const ItemDef& def = ItemRegistry::get(itemId);
        if (def.maxStack == 0) {
            return count;
        }

        for (ItemStack& stack : m_slots) {
            if (count == 0) {
                return 0;
            }
            if (stack.isEmpty() || stack.itemId != itemId || stack.count >= def.maxStack) {
                continue;
            }

            const uint16_t freeSpace = static_cast<uint16_t>(def.maxStack - stack.count);
            const uint16_t add = static_cast<uint16_t>(std::min<uint32_t>(count, freeSpace));
            stack.count = static_cast<uint16_t>(stack.count + add);
            count -= add;
        }

        for (ItemStack& stack : m_slots) {
            if (count == 0) {
                return 0;
            }
            if (!stack.isEmpty()) {
                continue;
            }

            const uint16_t add = static_cast<uint16_t>(std::min<uint32_t>(count, def.maxStack));
            stack.itemId = itemId;
            stack.count = add;
            stack.durability = def.isTool ? def.maxDurability : 0;
            count -= add;
        }

        return count;
    }

private:
    std::array<ItemStack, SLOT_COUNT> m_slots{};
};

class ChestInventoryStore {
public:
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
            return hx ^ (hy + 0x9e3779b9u + (hx << 6u) + (hx >> 2u)) ^
                   (hz + 0x9e3779b9u + (hy << 6u) + (hy >> 2u));
        }
    };

    [[nodiscard]] ChestInventory& getOrCreate(const glm::ivec3& position) {
        return m_chests[toKey(position)];
    }

    [[nodiscard]] const ChestInventory* find(const glm::ivec3& position) const {
        const auto it = m_chests.find(toKey(position));
        return it == m_chests.end() ? nullptr : &it->second;
    }

    [[nodiscard]] std::array<ItemStack, ChestInventory::SLOT_COUNT> extractAndErase(const glm::ivec3& position) {
        std::array<ItemStack, ChestInventory::SLOT_COUNT> contents{};
        const auto it = m_chests.find(toKey(position));
        if (it == m_chests.end()) {
            return contents;
        }

        for (int slot = 0; slot < ChestInventory::SLOT_COUNT; ++slot) {
            contents[static_cast<std::size_t>(slot)] = it->second.getSlotStack(slot);
        }
        m_chests.erase(it);
        return contents;
    }

    void erase(const glm::ivec3& position) {
        m_chests.erase(toKey(position));
    }

private:
    [[nodiscard]] static Key toKey(const glm::ivec3& position) {
        return {position.x, position.y, position.z};
    }

    std::unordered_map<Key, ChestInventory, KeyHash> m_chests;
};
