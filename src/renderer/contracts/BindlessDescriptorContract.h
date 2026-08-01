#ifndef MECRAFT_BINDLESS_DESCRIPTOR_CONTRACT_H
#define MECRAFT_BINDLESS_DESCRIPTOR_CONTRACT_H

#include <cstdint>
#include <limits>
#include <type_traits>

namespace renderer::contracts {

/// Identifies each binding in the frame-stable Vulkan global bindless set.
enum class GlobalBindlessBinding : uint32_t {
    SampledTexture2D = 0u,
    SampledTextureCube = 1u,
    Sampler = 2u,
    StorageBuffer = 3u,
    AccelerationStructure = 4u,
    Count = 5u
};

inline constexpr uint32_t kGlobalBindlessBindingCount = static_cast<uint32_t>(GlobalBindlessBinding::Count);
inline constexpr uint32_t kInvalidBindlessDescriptorIndex = std::numeric_limits<uint32_t>::max();

/// Stores one zero-based descriptor-array slot and its CPU-side publication generation.
/// Generation zero and the maximum index are reserved for invalid handles, while slot zero
/// remains available for explicit constant resources used by material records.
/// @tparam Tag Resource-array domain that prevents handles from different bindings being mixed.
template <typename Tag> struct BindlessDescriptorHandle final {
    uint32_t index = kInvalidBindlessDescriptorIndex;
    uint32_t generation = 0u;

    /// Reports whether this handle names one allocated descriptor slot generation.
    /// @return True when both the zero-based index and non-zero generation are valid.
    [[nodiscard]] constexpr bool isValid() const {
        return index != kInvalidBindlessDescriptorIndex && generation != 0u;
    }
};

/// Compares two handles from the same bindless resource-array domain.
/// @param lhs Left descriptor handle.
/// @param rhs Right descriptor handle.
/// @return True when both the index and generation match.
template <typename Tag>
[[nodiscard]] constexpr bool operator==(const BindlessDescriptorHandle<Tag> lhs,
                                        const BindlessDescriptorHandle<Tag> rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

/// Compares two handles from the same bindless resource-array domain for inequality.
/// @param lhs Left descriptor handle.
/// @param rhs Right descriptor handle.
/// @return True when either the index or generation differs.
template <typename Tag>
[[nodiscard]] constexpr bool operator!=(const BindlessDescriptorHandle<Tag> lhs,
                                        const BindlessDescriptorHandle<Tag> rhs) {
    return !(lhs == rhs);
}

struct BindlessTexture2DTag;
struct BindlessTextureCubeTag;
struct BindlessSamplerTag;
struct BindlessStorageBufferTag;

using BindlessTexture2DHandle = BindlessDescriptorHandle<BindlessTexture2DTag>;
using BindlessTextureCubeHandle = BindlessDescriptorHandle<BindlessTextureCubeTag>;
using BindlessSamplerHandle = BindlessDescriptorHandle<BindlessSamplerTag>;
using BindlessStorageBufferHandle = BindlessDescriptorHandle<BindlessStorageBufferTag>;

static_assert(sizeof(BindlessTexture2DHandle) == 8u);
static_assert(sizeof(BindlessTextureCubeHandle) == 8u);
static_assert(sizeof(BindlessSamplerHandle) == 8u);
static_assert(sizeof(BindlessStorageBufferHandle) == 8u);
static_assert(std::is_trivially_copyable_v<BindlessTexture2DHandle>);
static_assert(std::is_trivially_copyable_v<BindlessTextureCubeHandle>);
static_assert(std::is_trivially_copyable_v<BindlessSamplerHandle>);
static_assert(std::is_trivially_copyable_v<BindlessStorageBufferHandle>);
static_assert(std::is_standard_layout_v<BindlessTexture2DHandle>);
static_assert(std::is_standard_layout_v<BindlessTextureCubeHandle>);
static_assert(std::is_standard_layout_v<BindlessSamplerHandle>);
static_assert(std::is_standard_layout_v<BindlessStorageBufferHandle>);

} // namespace renderer::contracts

#endif // MECRAFT_BINDLESS_DESCRIPTOR_CONTRACT_H
