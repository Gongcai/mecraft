#pragma once

#include <vector>
#include <cstdint>
#include <cstddef>

class BitPackedArray {
public:
    BitPackedArray() = default;
    BitPackedArray(size_t count, uint8_t bitsPerEntry);

    // Read the entry at the given index
    [[nodiscard]] uint32_t get(size_t index) const;

    // Write the entry at the given index
    void set(size_t index, uint32_t value);

    // Resize to a new bit width (re-encodes all existing entries)
    void resize(uint8_t newBitsPerEntry);

    // Fill all entries with a value
    void fill(uint32_t value);

    [[nodiscard]] size_t size() const { return m_count; }
    [[nodiscard]] uint8_t bitsPerEntry() const { return m_bitsPerEntry; }
    [[nodiscard]] size_t dataByteSize() const { return m_data.size() * sizeof(uint64_t); }
    [[nodiscard]] size_t allocatedByteSize() const { return m_data.capacity() * sizeof(uint64_t); }

    // Direct access to raw 64-bit words for serialization.
    [[nodiscard]] const uint64_t* rawData() const { return m_data.data(); }
    [[nodiscard]] size_t rawDataWords() const { return m_data.size(); }

private:
    size_t m_count = 0;
    uint8_t m_bitsPerEntry = 0;
    std::vector<uint64_t> m_data;  // 64-bit words for storage

    static constexpr int BITS_PER_WORD = 64;
};
