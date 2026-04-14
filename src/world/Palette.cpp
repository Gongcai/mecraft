#include "Palette.h"
#include <cmath>
#include <unordered_set>

uint8_t Palette::getOrCreateIndex(RuntimeId runtimeId) {
    auto it = m_idToIndex.find(runtimeId);
    if (it != m_idToIndex.end()) {
        return it->second;
    }

    uint8_t index = static_cast<uint8_t>(m_indexToId.size());
    m_indexToId.push_back(runtimeId);
    m_idToIndex[runtimeId] = index;
    return index;
}

RuntimeId Palette::getRuntimeId(uint8_t paletteIndex) const {
    if (paletteIndex < m_indexToId.size()) {
        return m_indexToId[paletteIndex];
    }
    return RUNTIME_ID_NULL;
}

size_t Palette::size() const {
    return m_indexToId.size();
}

uint8_t Palette::bitsPerEntry() const {
    if (m_indexToId.size() <= 1) return 1;
    return static_cast<uint8_t>(std::ceil(std::log2(static_cast<double>(m_indexToId.size()))));
}

std::vector<uint8_t> Palette::compact(const std::vector<RuntimeId>& usedIds) {
    // Build old → new index mapping before modifying the palette.
    // oldToNew[oldIndex] = newIndex, or 0xFF if removed.
    std::vector<uint8_t> oldToNew(m_indexToId.size(), 0xFF);

    // Deduplicate while preserving order
    std::unordered_set<RuntimeId> seen;
    std::vector<RuntimeId> uniqueUsed;
    uniqueUsed.reserve(usedIds.size());
    for (RuntimeId id : usedIds) {
        if (seen.insert(id).second) {
            uniqueUsed.push_back(id);
        }
    }

    // Build oldToNew: for each unique used id, find its old index and assign a new one
    uint8_t newIndex = 0;
    for (RuntimeId id : uniqueUsed) {
        auto it = m_idToIndex.find(id);
        if (it != m_idToIndex.end()) {
            oldToNew[it->second] = newIndex;
            ++newIndex;
        }
    }

    // Rebuild palette
    m_indexToId.clear();
    m_idToIndex.clear();

    for (RuntimeId id : uniqueUsed) {
        uint8_t idx = static_cast<uint8_t>(m_indexToId.size());
        m_indexToId.push_back(id);
        m_idToIndex[id] = idx;
    }

    return oldToNew;
}

void Palette::clear() {
    m_indexToId.clear();
    m_idToIndex.clear();
}
