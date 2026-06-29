#pragma once

#include "BlockStateRegistry.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

class Palette {
public:
    uint32_t getOrCreateIndex(BlockStateId stateId);
    [[nodiscard]] BlockStateId getStateId(uint32_t paletteIndex) const;
    // Read a block state id when the caller has already validated the palette index.
    [[nodiscard]] BlockStateId getStateIdUnchecked(uint32_t paletteIndex) const {
        return m_indexToId[paletteIndex];
    }
    [[nodiscard]] size_t size() const;
    [[nodiscard]] uint8_t bitsPerEntry() const;
    [[nodiscard]] size_t dynamicMemoryBytes() const;
    std::vector<uint32_t> compact(const std::vector<BlockStateId>& usedIds);
    void clear();

private:
    std::vector<BlockStateId> m_indexToId;
    std::unordered_map<BlockStateId, uint32_t> m_idToIndex;
};
