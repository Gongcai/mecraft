#pragma once

#include "../../engine/registry/IdRegistry.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

class Palette {
public:
    uint16_t getOrCreateIndex(RuntimeId runtimeId);
    [[nodiscard]] RuntimeId getRuntimeId(uint16_t paletteIndex) const;
    // Read a runtime id when the caller has already validated the palette index.
    [[nodiscard]] RuntimeId getRuntimeIdUnchecked(uint16_t paletteIndex) const {
        return m_indexToId[paletteIndex];
    }
    [[nodiscard]] size_t size() const;
    [[nodiscard]] uint8_t bitsPerEntry() const;
    [[nodiscard]] size_t dynamicMemoryBytes() const;
    std::vector<uint16_t> compact(const std::vector<RuntimeId>& usedIds);
    void clear();

private:
    std::vector<RuntimeId> m_indexToId;
    std::unordered_map<RuntimeId, uint16_t> m_idToIndex;
};
