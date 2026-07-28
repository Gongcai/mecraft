#ifndef MECRAFT_CONTENT_HASH_CONTRACT_H
#define MECRAFT_CONTENT_HASH_CONTRACT_H

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace renderer::contracts {

using StableContentHash = uint64_t;
inline constexpr const char* kStableContentHashAlgorithm = "fnv1a64";

/// Incrementally builds a platform-independent FNV-1a 64-bit content hash.
class StableContentHashBuilder final {
public:
    /// Appends an uninterpreted byte sequence in its existing order.
    /// @param data Address of size readable bytes; null is valid only for zero bytes.
    /// @param size Number of bytes to append.
    void addBytes(const void* data, size_t size);

    /// Appends one unsigned integer in fixed little-endian byte order.
    /// @param value Integer value whose complete bit pattern is hashed.
    void addUint64(uint64_t value);

    /// Appends one signed integer using its modulo-2^64 representation.
    /// @param value Signed integer value to append.
    void addInt64(int64_t value);

    /// Appends one IEEE-754 double after canonicalizing signed zero.
    /// @param value Finite semantic value to append.
    void addDouble(double value);

    /// Appends a length-prefixed UTF-8 string.
    /// @param value String bytes whose length and content both affect the hash.
    void addString(std::string_view value);

    /// Returns the hash after every value appended so far.
    /// @return Stable non-zero 64-bit FNV-1a state.
    [[nodiscard]] StableContentHash value() const;

private:
    void addByte(uint8_t value);

    StableContentHash m_value = 14695981039346656037ull;
};

/// Identifies file hashing failures without exceptions.
enum class ContentHashError : uint8_t {
    None,
    FileOpenFailed,
    FileReadFailed
};

/// Returns either a complete file hash or one stable I/O error.
struct FileContentHashResult {
    StableContentHash hash = 0u;
    ContentHashError error = ContentHashError::None;

    /// Reports whether every file byte contributed to hash.
    /// @return True only when error is None and hash contains a valid value.
    [[nodiscard]] bool succeeded() const;
};

/// Hashes one in-memory byte sequence without semantic length framing.
/// @param data Address of size readable bytes; null is valid only for zero bytes.
/// @param size Number of bytes to hash.
/// @return Stable FNV-1a content hash.
[[nodiscard]] StableContentHash stableContentHashBytes(
    const void* data,
    size_t size);

/// Hashes every byte of one file using bounded streaming memory.
/// @param path File whose exact contents define the returned identity.
/// @return Complete content hash or a stable file error.
[[nodiscard]] FileContentHashResult stableFileContentHash(
    const std::filesystem::path& path);

/// Parses the canonical sixteen-digit lowercase hexadecimal representation.
/// @param text Hash text to validate and decode.
/// @param hash Receives the decoded value when parsing succeeds.
/// @return True only for exactly sixteen lowercase hexadecimal digits.
[[nodiscard]] bool parseStableContentHashHex(
    std::string_view text,
    StableContentHash& hash);

/// Formats one stable hash for scene assets, reports, and manifests.
/// @param hash Stable content hash to format.
/// @return Sixteen lowercase hexadecimal digits.
[[nodiscard]] std::string stableContentHashHex(StableContentHash hash);

/// Returns the stable identifier used by diagnostics and tests.
/// @param error Content hashing error to identify.
/// @return Process-lifetime error identifier.
[[nodiscard]] const char* contentHashErrorStableId(ContentHashError error);

} // namespace renderer::contracts

#endif // MECRAFT_CONTENT_HASH_CONTRACT_H
