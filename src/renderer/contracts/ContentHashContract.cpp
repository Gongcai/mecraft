#include "renderer/contracts/ContentHashContract.h"

#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>

namespace renderer::contracts {
namespace {

constexpr StableContentHash kFnvPrime = 1099511628211ull;

} // namespace

void StableContentHashBuilder::addBytes(const void* data, const size_t size) {
    if (data == nullptr && size != 0u) {
        std::abort();
    }
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t index = 0u; index < size; ++index) {
        addByte(bytes[index]);
    }
}

void StableContentHashBuilder::addUint64(const uint64_t value) {
    for (uint32_t byteIndex = 0u; byteIndex < 8u; ++byteIndex) {
        addByte(static_cast<uint8_t>(value >> (byteIndex * 8u)));
    }
}

void StableContentHashBuilder::addInt64(const int64_t value) {
    addUint64(static_cast<uint64_t>(value));
}

void StableContentHashBuilder::addDouble(const double value) {
    static_assert(sizeof(double) == sizeof(uint64_t));
    static_assert(std::numeric_limits<double>::is_iec559);
    uint64_t bits = 0u;
    const double canonicalValue = value == 0.0 ? 0.0 : value;
    std::memcpy(&bits, &canonicalValue, sizeof(bits));
    addUint64(bits);
}

void StableContentHashBuilder::addString(const std::string_view value) {
    addUint64(value.size());
    addBytes(value.data(), value.size());
}

StableContentHash StableContentHashBuilder::value() const {
    return m_value;
}

void StableContentHashBuilder::addByte(const uint8_t value) {
    m_value ^= value;
    m_value *= kFnvPrime;
}

bool FileContentHashResult::succeeded() const {
    return error == ContentHashError::None && hash != 0u;
}

StableContentHash stableContentHashBytes(const void* data, const size_t size) {
    StableContentHashBuilder hash;
    hash.addBytes(data, size);
    return hash.value();
}

FileContentHashResult stableFileContentHash(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {0u, ContentHashError::FileOpenFailed};
    }

    StableContentHashBuilder hash;
    std::array<char, 64u * 1024u> buffer{};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize readSize = input.gcount();
        if (readSize > 0) {
            hash.addBytes(buffer.data(), static_cast<size_t>(readSize));
        }
    }
    if (input.bad()) {
        return {0u, ContentHashError::FileReadFailed};
    }
    return {hash.value(), ContentHashError::None};
}

bool parseStableContentHashHex(const std::string_view text, StableContentHash& hash) {
    if (text.size() != 16u) {
        return false;
    }
    StableContentHash value = 0u;
    for (const char character : text) {
        uint8_t digit = 0u;
        if (character >= '0' && character <= '9') {
            digit = static_cast<uint8_t>(character - '0');
        } else if (character >= 'a' && character <= 'f') {
            digit = static_cast<uint8_t>(character - 'a' + 10);
        } else {
            return false;
        }
        value = (value << 4u) | digit;
    }
    hash = value;
    return true;
}

std::string stableContentHashHex(const StableContentHash hash) {
    constexpr char kHexDigits[] = "0123456789abcdef";
    std::array<char, 16u> characters{};
    for (size_t index = 0u; index < characters.size(); ++index) {
        const uint32_t shift = static_cast<uint32_t>((characters.size() - index - 1u) * 4u);
        characters[index] = kHexDigits[(hash >> shift) & 0x0fu];
    }
    return std::string(characters.data(), characters.size());
}

const char* contentHashErrorStableId(const ContentHashError error) {
    switch (error) {
    case ContentHashError::None: return "None";
    case ContentHashError::FileOpenFailed: return "FileOpenFailed";
    case ContentHashError::FileReadFailed: return "FileReadFailed";
    }
    std::abort();
}

} // namespace renderer::contracts
