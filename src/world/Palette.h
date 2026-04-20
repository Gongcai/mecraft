#pragma once

#include "../core/IdRegistry.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

class Palette {
public:
    uint16_t getOrCreateIndex(RuntimeId runtimeId);
    [[nodiscard]] RuntimeId getRuntimeId(uint16_t paletteIndex) const;
    [[nodiscard]] size_t size() const;
    [[nodiscard]] uint8_t bitsPerEntry() const;
    std::vector<uint16_t> compact(const std::vector<RuntimeId>& usedIds);
    void clear();

private:
    std::vector<RuntimeId> m_indexToId;
    std::unordered_map<RuntimeId, uint16_t> m_idToIndex;
};
