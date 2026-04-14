#include "BitPackedArray.h"
#include <algorithm>

BitPackedArray::BitPackedArray(size_t count, uint8_t bitsPerEntry)
    : m_count(count), m_bitsPerEntry(bitsPerEntry) {
    if (m_bitsPerEntry == 0) m_bitsPerEntry = 1;
    size_t entriesPerWord = BITS_PER_WORD / m_bitsPerEntry;
    size_t wordCount = (count + entriesPerWord - 1) / entriesPerWord;
    m_data.resize(wordCount, 0);
}

uint32_t BitPackedArray::get(size_t index) const {
    if (index >= m_count || m_bitsPerEntry == 0) return 0;

    size_t bitIndex = index * m_bitsPerEntry;
    size_t wordIndex = bitIndex / BITS_PER_WORD;
    size_t bitOffset = bitIndex % BITS_PER_WORD;

    uint64_t word = m_data[wordIndex];

    // If the entry spans two words
    if (bitOffset + m_bitsPerEntry > BITS_PER_WORD && wordIndex + 1 < m_data.size()) {
        uint64_t nextWord = m_data[wordIndex + 1];
        uint64_t lowBits = (word >> bitOffset);
        uint64_t highBits = (nextWord << (BITS_PER_WORD - bitOffset));
        uint64_t combined = lowBits | highBits;
        uint64_t mask = (1ULL << m_bitsPerEntry) - 1;
        return static_cast<uint32_t>(combined & mask);
    }

    uint64_t mask = (1ULL << m_bitsPerEntry) - 1;
    return static_cast<uint32_t>((word >> bitOffset) & mask);
}

void BitPackedArray::set(size_t index, uint32_t value) {
    if (index >= m_count || m_bitsPerEntry == 0) return;

    size_t bitIndex = index * m_bitsPerEntry;
    size_t wordIndex = bitIndex / BITS_PER_WORD;
    size_t bitOffset = bitIndex % BITS_PER_WORD;

    uint64_t mask = (1ULL << m_bitsPerEntry) - 1;
    uint64_t val = static_cast<uint64_t>(value & mask);

    // Clear the old bits and set the new ones
    if (bitOffset + m_bitsPerEntry <= BITS_PER_WORD) {
        // Fits in one word
        uint64_t clearMask = ~(mask << bitOffset);
        m_data[wordIndex] = (m_data[wordIndex] & clearMask) | (val << bitOffset);
    } else {
        // Spans two words
        int bitsInFirstWord = static_cast<int>(BITS_PER_WORD - bitOffset);
        int bitsInSecondWord = m_bitsPerEntry - bitsInFirstWord;

        uint64_t mask1 = (1ULL << bitsInFirstWord) - 1;
        uint64_t mask2 = (1ULL << bitsInSecondWord) - 1;

        // Clear and set in first word
        uint64_t clearMask1 = ~(mask1 << bitOffset);
        m_data[wordIndex] = (m_data[wordIndex] & clearMask1) | ((val & mask1) << bitOffset);

        // Clear and set in second word
        if (wordIndex + 1 < m_data.size()) {
            uint64_t clearMask2 = ~mask2;
            m_data[wordIndex + 1] = (m_data[wordIndex + 1] & clearMask2) | (val >> bitsInFirstWord);
        }
    }
}

void BitPackedArray::resize(uint8_t newBitsPerEntry) {
    if (newBitsPerEntry == m_bitsPerEntry) return;
    if (newBitsPerEntry == 0) newBitsPerEntry = 1;

    // Copy out all existing values
    std::vector<uint32_t> oldValues(m_count);
    for (size_t i = 0; i < m_count; ++i) {
        oldValues[i] = get(i);
    }

    // Reinitialize with new bit width
    m_bitsPerEntry = newBitsPerEntry;
    size_t entriesPerWord = BITS_PER_WORD / m_bitsPerEntry;
    size_t wordCount = (m_count + entriesPerWord - 1) / entriesPerWord;
    m_data.assign(wordCount, 0);

    // Re-encode all values
    for (size_t i = 0; i < m_count; ++i) {
        set(i, oldValues[i]);
    }
}

void BitPackedArray::fill(uint32_t value) {
    for (size_t i = 0; i < m_count; ++i) {
        set(i, value);
    }
}
