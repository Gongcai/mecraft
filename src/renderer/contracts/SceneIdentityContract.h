#ifndef MECRAFT_SCENE_IDENTITY_CONTRACT_H
#define MECRAFT_SCENE_IDENTITY_CONTRACT_H

#include <atomic>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>

namespace renderer::contracts {

/// Strong 32-bit identifier whose non-zero value remains unchanged for one visible lifetime.
/// Scene owners allocate values and retire all referencing histories before reusing a value.
/// @tparam Tag Unique type tag that prevents IDs from different scene domains being mixed.
template <typename Tag>
struct StableSceneId final {
    uint32_t value = 0u;

    /// Determine whether this ID names a scene record.
    /// @return True for non-zero IDs; zero is reserved as the explicit invalid value.
    [[nodiscard]] constexpr bool isValid() const {
        return value != 0u;
    }
};

/// Compare stable IDs within the same scene-identity domain.
/// @param lhs Left stable ID.
/// @param rhs Right stable ID.
/// @return True when both IDs contain the same numeric identity.
template <typename Tag>
[[nodiscard]] constexpr bool operator==(const StableSceneId<Tag> lhs,
                                        const StableSceneId<Tag> rhs) {
    return lhs.value == rhs.value;
}

/// Compare stable IDs within the same scene-identity domain for inequality.
/// @param lhs Left stable ID.
/// @param rhs Right stable ID.
/// @return True when the numeric identities differ.
template <typename Tag>
[[nodiscard]] constexpr bool operator!=(const StableSceneId<Tag> lhs,
                                        const StableSceneId<Tag> rhs) {
    return !(lhs == rhs);
}

struct StableObjectIdTag;
struct StableMaterialIdTag;
struct StableGeometryIdTag;
struct StableLightIdTag;
struct StableReflectionProbeIdTag;

using StableObjectId = StableSceneId<StableObjectIdTag>;
using StableMaterialId = StableSceneId<StableMaterialIdTag>;
using StableGeometryId = StableSceneId<StableGeometryIdTag>;
using StableLightId = StableSceneId<StableLightIdTag>;
using StableReflectionProbeId = StableSceneId<StableReflectionProbeIdTag>;

/// Reserves the leading material-ID range for immutable voxel texture layers.
/// Dynamic material resources start after this range so a voxel layer can be
/// converted to a stable ID directly in both CPU and shader code.
inline constexpr uint32_t kVoxelMaterialIdCapacity = 1u << 20u;

template <typename Tag>
struct StableSceneIdAllocationStart final {
    static constexpr uint32_t value = 1u;
};

template <>
struct StableSceneIdAllocationStart<StableMaterialIdTag> final {
    static constexpr uint32_t value = kVoxelMaterialIdCapacity + 1u;
};

/// Allocates one process-unique stable scene ID without reusing retired values.
/// The atomic counter is independent for every strongly typed identity domain.
/// @tparam Tag Identity domain selected by StableSceneId aliases.
/// @return Newly allocated non-zero ID, or std::nullopt after exhausting the
/// complete representable range.
template <typename Tag>
[[nodiscard]] std::optional<StableSceneId<Tag>> allocateStableSceneId() {
    static std::atomic<uint32_t> next{
        StableSceneIdAllocationStart<Tag>::value};
    uint32_t candidate = next.load(std::memory_order_relaxed);
    while (candidate != std::numeric_limits<uint32_t>::max()) {
        if (next.compare_exchange_weak(
                candidate,
                candidate + 1u,
                std::memory_order_relaxed,
                std::memory_order_relaxed)) {
            return StableSceneId<Tag>{candidate};
        }
    }
    return std::nullopt;
}

/// Converts one immutable voxel texture-array layer to its reserved material ID.
/// Animation frame offsets are deliberately excluded from this conversion.
/// @param baseLayer Base texture-array layer before animation frame selection.
/// @return Stable non-zero material ID, or std::nullopt when the layer exceeds
/// the frozen voxel material range.
[[nodiscard]] constexpr std::optional<StableMaterialId>
stableMaterialIdForVoxelLayer(const uint32_t baseLayer) {
    return baseLayer < kVoxelMaterialIdCapacity
        ? std::optional<StableMaterialId>{StableMaterialId{baseLayer + 1u}}
        : std::nullopt;
}

static_assert(sizeof(StableObjectId) == sizeof(uint32_t));
static_assert(sizeof(StableMaterialId) == sizeof(uint32_t));
static_assert(sizeof(StableGeometryId) == sizeof(uint32_t));
static_assert(sizeof(StableLightId) == sizeof(uint32_t));
static_assert(sizeof(StableReflectionProbeId) == sizeof(uint32_t));
static_assert(std::is_trivially_copyable_v<StableObjectId>);
static_assert(std::is_trivially_copyable_v<StableMaterialId>);
static_assert(std::is_trivially_copyable_v<StableGeometryId>);
static_assert(std::is_trivially_copyable_v<StableLightId>);
static_assert(std::is_trivially_copyable_v<StableReflectionProbeId>);
static_assert(std::is_standard_layout_v<StableObjectId>);
static_assert(std::is_standard_layout_v<StableMaterialId>);
static_assert(std::is_standard_layout_v<StableGeometryId>);
static_assert(std::is_standard_layout_v<StableLightId>);
static_assert(std::is_standard_layout_v<StableReflectionProbeId>);

} // namespace renderer::contracts

#endif // MECRAFT_SCENE_IDENTITY_CONTRACT_H
