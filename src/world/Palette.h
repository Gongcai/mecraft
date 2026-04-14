#pragma once

#include "../core/IdRegistry.h"
#include <vector>
#include <cstdint>
#include <unordered_map>

class Palette {
public:
    // Get or create a palette index for the given runtime ID
    uint8_t getOrCreateIndex(RuntimeId runtimeId);

    // Get the runtime ID for a given palette index
    [[nodiscard]] RuntimeId getRuntimeId(uint8_t paletteIndex) const;

    // Number of entries in the palette
    [[nodiscard]] size_t size() const;

    // Bit width needed = ceil(log2(max(size(), 2)))
    [[nodiscard]] uint8_t bitsPerEntry() const;

    // Rebuild palette with only the given used RuntimeIds (in order of appearance).
    // Returns a mapping: old palette index → new palette index.
    // If an old index is not in the mapping, it was removed.
    std::vector<uint8_t> compact(const std::vector<RuntimeId>& usedIds);

    // Clear all entries
    void clear();

private:
    // palette index → RuntimeId (compact)
    std::vector<RuntimeId> m_indexToId;

    // RuntimeId → palette index (fast reverse lookup)
    std::unordered_map<RuntimeId, uint8_t> m_idToIndex;
};
