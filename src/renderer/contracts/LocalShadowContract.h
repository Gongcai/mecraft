#ifndef MECRAFT_LOCAL_SHADOW_CONTRACT_H
#define MECRAFT_LOCAL_SHADOW_CONTRACT_H

#include "GpuLightContract.h"

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <array>
#include <cstdint>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace renderer::contracts {

inline constexpr uint32_t kLocalShadowContractVersion = 1u;
inline constexpr uint32_t kLocalShadowSpotTileResolution = 512u;
inline constexpr uint32_t kLocalShadowPointFaceResolution = 256u;
inline constexpr uint32_t kLocalShadowMaxSpotLightCount = 64u;
inline constexpr uint32_t kLocalShadowMaxPointLightCount = 64u;
inline constexpr uint32_t kLocalShadowPointMetadataBase = kLocalShadowMaxSpotLightCount;
inline constexpr uint32_t kLocalShadowMetadataCount = kLocalShadowMaxSpotLightCount + kLocalShadowMaxPointLightCount;
inline constexpr float kLocalShadowNearPlaneMeters = 0.05f;
inline constexpr float kLocalShadowDepthBiasMeters = 0.005f;
inline constexpr float kLocalShadowNormalOffsetMeters = 0.005f;
inline constexpr uint64_t kLocalShadowGeometrySignatureSeed = 1469598103934665603ULL;

/// Classifies the persistent raster resource used by one local light.
enum class LocalShadowType : uint32_t { Spot = 0u, Point = 1u };

/// Mirrors one fixed-size local-shadow metadata record consumed by GLSL.
struct alignas(16) LocalShadowMetadata final {
    /// Camera-relative view-projection matrices. Spot lights use element zero;
    /// Point lights use the +X, -X, +Y, -Y, +Z, -Z face order.
    std::array<glm::mat4, 6> cameraRelativeViewProjection{};
    /// Spot atlas scale.xy and bias.zw. Point lights store zero.
    glm::vec4 atlasScaleBias{0.0f};
    /// Near plane, far plane, linear-depth comparison bias, and normal offset in meters.
    glm::vec4 nearFarDepthBiasNormalOffset{0.0f};
    /// LocalShadowType, resource slot, face count, and contract version.
    glm::uvec4 classification{0u};
};

/// Describes one stable raster allocation selected for a scene light.
struct LocalShadowAllocation final {
    uint32_t sceneLightIndex = 0u;
    StableLightId lightId;
    LocalShadowType type = LocalShadowType::Spot;
    GpuLightShadowPolicy policy = GpuLightShadowPolicy::None;
    uint32_t resourceSlot = 0u;
    uint32_t metadataIndex = 0u;
};

/// Identifies every deterministic stable-allocation failure.
enum class LocalShadowAllocationError : uint8_t {
    None,
    InvalidSceneLight,
    DuplicateStableId,
    UnsupportedLightType,
    RayQueryUnavailable,
    StableIdTypeChanged,
    SpotCapacityExceeded,
    PointCapacityExceeded
};

/// Carries the exact stable ID associated with an allocation failure.
struct LocalShadowAllocationFailure final {
    LocalShadowAllocationError error = LocalShadowAllocationError::None;
    StableLightId lightId;
};

/// Maintains stable Spot and Point slots across complete scene snapshots.
/// Allocation is transactional: capacity or validation failure leaves the
/// previous map untouched and publishes no partial result.
class LocalShadowStableAllocator final {
public:
    /// Allocates every raster-shadow request in one complete scene snapshot.
    /// Existing stable IDs retain their slots, and new IDs receive the lowest
    /// available slot after deterministic stable-ID ordering.
    /// @param sceneLights Complete unallocated scene-light snapshot.
    /// @param allocations Destination replaced only after a successful commit.
    /// @return True when every raster request received a stable slot.
    [[nodiscard]] bool allocate(const std::vector<SceneLight>& sceneLights,
                                std::vector<LocalShadowAllocation>& allocations);

    /// Clears all persistent stable-ID ownership and failure state.
    void reset();

    /// Returns the most recent structured allocation failure.
    [[nodiscard]] const LocalShadowAllocationFailure& failure() const { return m_failure; }

private:
    struct Record final {
        LocalShadowType type = LocalShadowType::Spot;
        uint32_t slot = 0u;
    };

    std::unordered_map<uint32_t, Record> m_records;
    LocalShadowAllocationFailure m_failure;
};

/// Returns the metadata array index assigned to one resource slot.
/// @param type Raster shadow resource class.
/// @param resourceSlot Zero-based slot within that resource class.
/// @return Fixed metadata index shared by CPU and GLSL.
[[nodiscard]] constexpr uint32_t localShadowMetadataIndex(const LocalShadowType type, const uint32_t resourceSlot) {
    return type == LocalShadowType::Spot ? resourceSlot : kLocalShadowPointMetadataBase + resourceSlot;
}

/// Returns the stable identifier used by logs and tests for one failure.
/// @param error Allocation error to identify.
/// @return Process-lifetime string containing the stable identifier.
[[nodiscard]] const char* localShadowAllocationErrorStableId(LocalShadowAllocationError error);

/// Extends the deterministic signature of terrain geometry relevant to one local-shadow volume.
/// @param signature Signature accumulated from preceding sub-chunks in stable traversal order.
/// @param chunkKey Stable world chunk key containing the sub-chunk.
/// @param subChunkIndex Vertical sub-chunk index within the chunk.
/// @param meshRevision Logical mesh revision requested by world edits.
/// @param meshFingerprint Resident GPU mesh allocation fingerprint.
/// @param resident True when the mesh ranges are available in the global GPU pool.
/// @return Updated non-zero signature used to validate a cached local-shadow page.
[[nodiscard]] uint64_t extendLocalShadowGeometrySignature(uint64_t signature, int64_t chunkKey,
                                                          uint32_t subChunkIndex, uint64_t meshRevision,
                                                          uint64_t meshFingerprint, bool resident);

static_assert(sizeof(LocalShadowMetadata) == 432u);
static_assert(alignof(LocalShadowMetadata) == 16u);
static_assert(std::is_trivially_copyable_v<LocalShadowMetadata>);
static_assert(std::is_standard_layout_v<LocalShadowMetadata>);

} // namespace renderer::contracts

#endif // MECRAFT_LOCAL_SHADOW_CONTRACT_H
