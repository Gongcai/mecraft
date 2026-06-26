#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include <glm/vec3.hpp>

#include "../../item/Item.h"

class BlockEntityInventory {
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

class BlockEntityInventoryStore {
public:
    [[nodiscard]] BlockEntityInventory& getOrCreate(const glm::ivec3& position,
                                                    const std::string& typeId,
                                                    const int slotCount) {
        validateTypeAndSlotCount(typeId, slotCount);
        Entry& entry = m_entries[toKey(position)];
        if (entry.typeId.empty()) {
            entry.typeId = typeId;
            entry.slotCount = slotCount;
            return entry.inventory;
        }
        if (entry.typeId != typeId || entry.slotCount != slotCount) {
            throw std::runtime_error("Block entity inventory metadata mismatch at position");
        }
        return entry.inventory;
    }

    [[nodiscard]] const BlockEntityInventory* find(const glm::ivec3& position) const {
        const auto it = m_entries.find(toKey(position));
        return it == m_entries.end() ? nullptr : &it->second.inventory;
    }

    [[nodiscard]] bool empty() const {
        return m_entries.empty();
    }

    template <typename Fn>
    void forEach(Fn&& fn) const {
        for (const auto& [key, entry] : m_entries) {
            fn(glm::ivec3(key.x, key.y, key.z), entry.typeId, entry.slotCount, entry.inventory);
        }
    }

    [[nodiscard]] std::array<ItemStack, BlockEntityInventory::SLOT_COUNT> extractAndErase(const glm::ivec3& position) {
        std::array<ItemStack, BlockEntityInventory::SLOT_COUNT> contents{};
        const auto it = m_entries.find(toKey(position));
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
        m_entries.erase(toKey(position));
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
            return hx ^ (hy + 0x9e3779b9u + (hx << 6u) + (hx >> 2u)) ^
                   (hz + 0x9e3779b9u + (hy << 6u) + (hy >> 2u));
        }
    };

    struct Entry {
        std::string typeId;
        int slotCount = 0;
        BlockEntityInventory inventory;
    };

    static void validateTypeAndSlotCount(const std::string& typeId, const int slotCount) {
        if (typeId.empty()) {
            throw std::runtime_error("Block entity inventory requires a type id");
        }
        if (slotCount <= 0 || slotCount > BlockEntityInventory::SLOT_COUNT) {
            throw std::runtime_error("Block entity inventory slot count is outside the supported range");
        }
    }

    [[nodiscard]] static Key toKey(const glm::ivec3& position) {
        return {position.x, position.y, position.z};
    }

    std::unordered_map<Key, Entry, KeyHash> m_entries;
};
