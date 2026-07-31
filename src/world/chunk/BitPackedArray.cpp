#include "BitPackedArray.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace {

[[noreturn]] void failBitPackedArray(const std::string& message) {
    std::cerr << message << '\n';
    std::abort();
}

} // namespace

uint8_t BitPackedArray::normalizeBitsPerEntry(uint8_t bitsPerEntry) {
    if (bitsPerEntry == 0) {
        bitsPerEntry = 1;
    }
    if (bitsPerEntry > 32) {
        failBitPackedArray("BitPackedArray supports at most 32 bits per entry");
    }
    return bitsPerEntry;
}

size_t BitPackedArray::wordCountFor(const size_t count, const uint8_t bitsPerEntry) {
    if (count == 0) {
        return 0;
    }
    if (count > std::numeric_limits<size_t>::max() / bitsPerEntry) {
        failBitPackedArray("BitPackedArray bit count exceeds size_t capacity");
    }
    const size_t totalBits = count * static_cast<size_t>(bitsPerEntry);
    return (totalBits + BITS_PER_WORD - 1) / BITS_PER_WORD;
}

uint64_t BitPackedArray::valueMask(const uint8_t bitsPerEntry) {
    return bitsPerEntry == 32 ? 0xFFFFFFFFULL : ((1ULL << bitsPerEntry) - 1ULL);
}

BitPackedArray::BitPackedArray(size_t count, uint8_t bitsPerEntry)
    : m_count(count), m_bitsPerEntry(normalizeBitsPerEntry(bitsPerEntry)) {
    m_data.resize(wordCountFor(m_count, m_bitsPerEntry), 0);
}

uint32_t BitPackedArray::get(size_t index) const {
    if (index >= m_count || m_bitsPerEntry == 0)
        return 0;

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
        uint64_t mask = valueMask(m_bitsPerEntry);
        return static_cast<uint32_t>(combined & mask);
    }

    uint64_t mask = valueMask(m_bitsPerEntry);
    return static_cast<uint32_t>((word >> bitOffset) & mask);
}

void BitPackedArray::set(size_t index, uint32_t value) {
    if (index >= m_count || m_bitsPerEntry == 0)
        return;

    size_t bitIndex = index * m_bitsPerEntry;
    size_t wordIndex = bitIndex / BITS_PER_WORD;
    size_t bitOffset = bitIndex % BITS_PER_WORD;

    uint64_t mask = valueMask(m_bitsPerEntry);
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
    if (newBitsPerEntry == m_bitsPerEntry)
        return;
    newBitsPerEntry = normalizeBitsPerEntry(newBitsPerEntry);

    // Copy out all existing values
    std::vector<uint32_t> oldValues(m_count);
    for (size_t i = 0; i < m_count; ++i) {
        oldValues[i] = get(i);
    }

    // Reinitialize with new bit width
    m_bitsPerEntry = newBitsPerEntry;
    m_data.assign(wordCountFor(m_count, m_bitsPerEntry), 0);

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
