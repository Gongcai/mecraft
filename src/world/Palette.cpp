#include "Palette.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

uint16_t Palette::getOrCreateIndex(const RuntimeId runtimeId) {
    const auto it = m_idToIndex.find(runtimeId);
    if (it != m_idToIndex.end()) {
        return it->second;
    }

    const uint16_t index = static_cast<uint16_t>(m_indexToId.size());
    m_indexToId.push_back(runtimeId);
    m_idToIndex[runtimeId] = index;
    return index;
}

RuntimeId Palette::getRuntimeId(const uint16_t paletteIndex) const {
    if (paletteIndex < m_indexToId.size()) {
        return m_indexToId[paletteIndex];
    }
    return RUNTIME_ID_NULL;
}

size_t Palette::size() const {
    return m_indexToId.size();
}

uint8_t Palette::bitsPerEntry() const {
    if (m_indexToId.size() <= 1) {
        return 1;
    }

    const double bits = std::ceil(std::log2(static_cast<double>(m_indexToId.size())));
    return static_cast<uint8_t>(std::clamp(bits, 1.0, 16.0));
}

std::vector<uint16_t> Palette::compact(const std::vector<RuntimeId>& usedIds) {
    std::vector<uint16_t> oldToNew(m_indexToId.size(), UINT16_MAX);

    std::unordered_set<RuntimeId> seen;
    std::vector<RuntimeId> uniqueUsed;
    uniqueUsed.reserve(usedIds.size());
    for (const RuntimeId id : usedIds) {
        if (seen.insert(id).second) {
            uniqueUsed.push_back(id);
        }
    }

    uint16_t newIndex = 0;
    for (const RuntimeId id : uniqueUsed) {
        const auto it = m_idToIndex.find(id);
        if (it != m_idToIndex.end()) {
            oldToNew[it->second] = newIndex++;
        }
    }

    m_indexToId.clear();
    m_idToIndex.clear();
    for (const RuntimeId id : uniqueUsed) {
        const uint16_t idx = static_cast<uint16_t>(m_indexToId.size());
        m_indexToId.push_back(id);
        m_idToIndex[id] = idx;
    }

    return oldToNew;
}

void Palette::clear() {
    m_indexToId.clear();
    m_idToIndex.clear();
}
