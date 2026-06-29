#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include <glm/vec3.hpp>

#include "../block/PropIndices.h"
#include "WireFaceGeometry.h"

namespace WireConnectionBits {
constexpr uint8_t AXIS1_POS = 1u << 0u;
constexpr uint8_t AXIS1_NEG = 1u << 1u;
constexpr uint8_t AXIS2_POS = 1u << 2u;
constexpr uint8_t AXIS2_NEG = 1u << 3u;
constexpr uint8_t ALL = AXIS1_POS | AXIS1_NEG | AXIS2_POS | AXIS2_NEG;
} // namespace WireConnectionBits

struct WirePart {
    uint16_t channelId = 0;
    uint16_t facing = PropIndices::INVALID;
    uint8_t power = 0;
    uint8_t connections = 0;

    [[nodiscard]] bool sameSlot(const uint16_t otherChannelId, const uint16_t otherFacing) const {
        return channelId == otherChannelId && facing == otherFacing;
    }
};

class WireContainerParts {
public:
    static constexpr std::size_t MAX_PARTS = 24;

    [[nodiscard]] bool addPart(const WirePart& part) {
        validatePart(part);
        if (find(part.channelId, part.facing) != nullptr) {
            return false;
        }
        if (m_count >= MAX_PARTS) {
            return false;
        }

        m_parts[m_count] = part;
        ++m_count;
        return true;
    }

    [[nodiscard]] bool removePart(const uint16_t channelId, const uint16_t facing) {
        const std::size_t index = findIndex(channelId, facing);
        if (index == npos()) {
            return false;
        }

        for (std::size_t i = index + 1; i < m_count; ++i) {
            m_parts[i - 1] = m_parts[i];
        }
        --m_count;
        m_parts[m_count] = {};
        return true;
    }

    [[nodiscard]] const WirePart* find(const uint16_t channelId, const uint16_t facing) const {
        const std::size_t index = findIndex(channelId, facing);
        return index == npos() ? nullptr : &m_parts[index];
    }

    [[nodiscard]] WirePart* findMutable(const uint16_t channelId, const uint16_t facing) {
        const std::size_t index = findIndex(channelId, facing);
        return index == npos() ? nullptr : &m_parts[index];
    }

    [[nodiscard]] bool setPower(const uint16_t channelId, const uint16_t facing, const uint8_t power) {
        if (power > 15) {
            throw std::runtime_error("Wire container part power must be in the range 0 through 15");
        }
        WirePart* part = findMutable(channelId, facing);
        if (part == nullptr) {
            return false;
        }
        part->power = power;
        return true;
    }

    [[nodiscard]] std::size_t size() const {
        return m_count;
    }

    [[nodiscard]] bool empty() const {
        return m_count == 0;
    }

    void clear() {
        for (std::size_t i = 0; i < m_count; ++i) {
            m_parts[i] = {};
        }
        m_count = 0;
    }

    template <typename Fn>
    void forEach(Fn&& fn) const {
        for (std::size_t i = 0; i < m_count; ++i) {
            fn(m_parts[i]);
        }
    }

private:
    static constexpr std::size_t npos() {
        return static_cast<std::size_t>(-1);
    }

    static void validatePart(const WirePart& part) {
        if (part.channelId == 0) {
            throw std::runtime_error("Wire container part requires a non-zero channel id");
        }
        if (!WireFaceGeometry::isWireFacing(part.facing)) {
            throw std::runtime_error("Wire container part requires a supported wire facing");
        }
        if (part.power > 15) {
            throw std::runtime_error("Wire container part power must be in the range 0 through 15");
        }
        if ((part.connections & ~WireConnectionBits::ALL) != 0) {
            throw std::runtime_error("Wire container part connections must use the lower four bits");
        }
    }

    [[nodiscard]] std::size_t findIndex(const uint16_t channelId, const uint16_t facing) const {
        for (std::size_t i = 0; i < m_count; ++i) {
            if (m_parts[i].sameSlot(channelId, facing)) {
                return i;
            }
        }
        return npos();
    }

    std::array<WirePart, MAX_PARTS> m_parts{};
    std::size_t m_count = 0;
};

class WireContainerPartStore {
public:
    [[nodiscard]] WireContainerParts& getOrCreate(const glm::ivec3& position) {
        return m_entries[toKey(position)];
    }

    [[nodiscard]] const WireContainerParts* find(const glm::ivec3& position) const {
        const auto it = m_entries.find(toKey(position));
        return it == m_entries.end() ? nullptr : &it->second;
    }

    [[nodiscard]] WireContainerParts* findMutable(const glm::ivec3& position) {
        const auto it = m_entries.find(toKey(position));
        return it == m_entries.end() ? nullptr : &it->second;
    }

    [[nodiscard]] WireContainerParts extractAndErase(const glm::ivec3& position) {
        WireContainerParts parts;
        const auto it = m_entries.find(toKey(position));
        if (it == m_entries.end()) {
            return parts;
        }
        parts = it->second;
        m_entries.erase(it);
        return parts;
    }

    void erase(const glm::ivec3& position) {
        m_entries.erase(toKey(position));
    }

    void clear() {
        m_entries.clear();
    }

    [[nodiscard]] bool empty() const {
        return m_entries.empty();
    }

    [[nodiscard]] std::size_t size() const {
        return m_entries.size();
    }

    template <typename Fn>
    void forEach(Fn&& fn) const {
        for (const auto& [key, parts] : m_entries) {
            fn(glm::ivec3(key.x, key.y, key.z), parts);
        }
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

    [[nodiscard]] static Key toKey(const glm::ivec3& position) {
        return {position.x, position.y, position.z};
    }

    std::unordered_map<Key, WireContainerParts, KeyHash> m_entries;
};
