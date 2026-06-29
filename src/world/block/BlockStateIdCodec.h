#pragma once

#include "BlockStateRegistry.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace block_state_codec {

inline void writeVarUInt64(std::vector<uint8_t>& out, uint64_t value) {
    while (value >= 0x80u) {
        out.push_back(static_cast<uint8_t>((value & 0x7Fu) | 0x80u));
        value >>= 7u;
    }
    out.push_back(static_cast<uint8_t>(value));
}

inline bool readVarUInt64(const uint8_t*& cursor, const uint8_t* end, uint64_t& out) {
    out = 0;
    uint32_t shift = 0;
    for (uint32_t byteIndex = 0; byteIndex < 10; ++byteIndex) {
        if (cursor >= end) {
            return false;
        }

        const uint8_t byte = *cursor++;
        const uint64_t payload = static_cast<uint64_t>(byte & 0x7Fu);
        if (byteIndex == 9 && payload > 0x01u) {
            return false;
        }

        out |= payload << shift;
        if ((byte & 0x80u) == 0) {
            return true;
        }
        shift += 7u;
    }
    return false;
}

inline bool decodeRegistryIndex(const uint64_t encoded, BlockStateId& out) {
    if (encoded > static_cast<uint64_t>(std::numeric_limits<BlockStateId::Index>::max())) {
        return false;
    }
    out = BlockStateId::fromRegistryIndex(static_cast<BlockStateId::Index>(encoded));
    return true;
}

inline bool isRegistered(const BlockStateId stateId) {
    return stateId.registryIndex() < BlockStateRegistry::getStateCount();
}

inline void writeBlockStateId(std::vector<uint8_t>& out, const BlockStateId stateId) {
    writeVarUInt64(out, static_cast<uint64_t>(stateId.registryIndex()));
}

inline bool readBlockStateId(const uint8_t*& cursor, const uint8_t* end, BlockStateId& out) {
    uint64_t encoded = 0;
    return readVarUInt64(cursor, end, encoded) && decodeRegistryIndex(encoded, out);
}

inline bool readBlockStateId(const uint8_t* data, const size_t size, size_t& offset, BlockStateId& out) {
    if (offset > size) {
        return false;
    }

    const uint8_t* cursor = data + offset;
    const uint8_t* end = data + size;
    if (!readBlockStateId(cursor, end, out)) {
        return false;
    }

    offset = static_cast<size_t>(cursor - data);
    return true;
}

inline bool readRegisteredBlockStateId(const uint8_t*& cursor, const uint8_t* end, BlockStateId& out) {
    if (!readBlockStateId(cursor, end, out)) {
        return false;
    }
    return isRegistered(out);
}

} // namespace block_state_codec
