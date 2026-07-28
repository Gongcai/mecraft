#ifndef MECRAFT_SCENE_IDENTITY_CONTRACT_H
#define MECRAFT_SCENE_IDENTITY_CONTRACT_H

#include <cstdint>
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

using StableObjectId = StableSceneId<StableObjectIdTag>;
using StableMaterialId = StableSceneId<StableMaterialIdTag>;
using StableGeometryId = StableSceneId<StableGeometryIdTag>;

static_assert(sizeof(StableObjectId) == sizeof(uint32_t));
static_assert(sizeof(StableMaterialId) == sizeof(uint32_t));
static_assert(sizeof(StableGeometryId) == sizeof(uint32_t));
static_assert(std::is_trivially_copyable_v<StableObjectId>);
static_assert(std::is_trivially_copyable_v<StableMaterialId>);
static_assert(std::is_trivially_copyable_v<StableGeometryId>);
static_assert(std::is_standard_layout_v<StableObjectId>);
static_assert(std::is_standard_layout_v<StableMaterialId>);
static_assert(std::is_standard_layout_v<StableGeometryId>);

} // namespace renderer::contracts

#endif // MECRAFT_SCENE_IDENTITY_CONTRACT_H
