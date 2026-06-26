#pragma once

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include <glm/vec3.hpp>

#include "ChestInventoryStore.h"

class BlockEntityInventoryStore {
public:
    [[nodiscard]] ChestInventory& getOrCreate(const glm::ivec3& position,
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

    [[nodiscard]] const ChestInventory* find(const glm::ivec3& position) const {
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

    [[nodiscard]] std::array<ItemStack, ChestInventory::SLOT_COUNT> extractAndErase(const glm::ivec3& position) {
        std::array<ItemStack, ChestInventory::SLOT_COUNT> contents{};
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
        ChestInventory inventory;
    };

    static void validateTypeAndSlotCount(const std::string& typeId, const int slotCount) {
        if (typeId.empty()) {
            throw std::runtime_error("Block entity inventory requires a type id");
        }
        if (slotCount <= 0 || slotCount > ChestInventory::SLOT_COUNT) {
            throw std::runtime_error("Block entity inventory slot count is outside the supported range");
        }
    }

    [[nodiscard]] static Key toKey(const glm::ivec3& position) {
        return {position.x, position.y, position.z};
    }

    std::unordered_map<Key, Entry, KeyHash> m_entries;
};
