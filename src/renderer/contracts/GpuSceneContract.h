#ifndef MECRAFT_GPU_SCENE_CONTRACT_H
#define MECRAFT_GPU_SCENE_CONTRACT_H

#include "renderer/contracts/SceneIdentityContract.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace renderer::contracts {

inline constexpr uint32_t kGpuSceneContractVersion = 1u;
inline constexpr uint32_t kGpuSceneInvalidTableIndex = std::numeric_limits<uint32_t>::max();
inline constexpr uint32_t kGpuSceneInvalidRayTracingInstanceId = std::numeric_limits<uint32_t>::max();
inline constexpr uint32_t kGpuSceneMaxRayTracingInstanceId = (1u << 24u) - 1u;

/// Packs one 64-bit device address into the two 32-bit words mirrored by GLSL uvec2.
struct alignas(8) GpuSceneDeviceAddress final {
    uint32_t low = 0u;
    uint32_t high = 0u;

    /// Reports whether the packed address names GPU memory.
    /// @return True when at least one address word is non-zero.
    [[nodiscard]] constexpr bool isValid() const { return low != 0u || high != 0u; }
};

/// Converts one native device address into the fixed CPU/GPU word representation.
/// @param address Unsigned native device address.
/// @return Low and high 32-bit words in shader-visible order.
[[nodiscard]] constexpr GpuSceneDeviceAddress packGpuSceneDeviceAddress(const uint64_t address) {
    return {static_cast<uint32_t>(address), static_cast<uint32_t>(address >> 32u)};
}

/// Reconstructs one native device address from its fixed CPU/GPU word representation.
/// @param address Low and high 32-bit words read from a scene record.
/// @return Unsigned 64-bit native device address.
[[nodiscard]] constexpr uint64_t unpackGpuSceneDeviceAddress(const GpuSceneDeviceAddress address) {
    return (static_cast<uint64_t>(address.high) << 32u) | address.low;
}

/// Stores three affine transform rows without relying on implementation-defined matrix padding.
struct alignas(16) GpuSceneAffineTransform final {
    glm::vec4 row0{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec4 row1{0.0f, 1.0f, 0.0f, 0.0f};
    glm::vec4 row2{0.0f, 0.0f, 1.0f, 0.0f};
};

/// Controls frame-scene participation without changing the fixed instance layout.
enum class GpuSceneInstanceFlag : uint32_t {
    Enabled = 1u << 0u,
    DynamicTransform = 1u << 1u,
    ShadowCaster = 1u << 2u,
    ReflectionVisible = 1u << 3u,
    RayTracingVisible = 1u << 4u,
    FirstPerson = 1u << 5u
};

using GpuSceneInstanceFlags = uint32_t;

/// Returns the bit corresponding to one instance participation rule.
/// @param flag Participation rule to encode.
/// @return Unsigned mask containing exactly one rule bit.
[[nodiscard]] constexpr GpuSceneInstanceFlags gpuSceneInstanceFlagBit(const GpuSceneInstanceFlag flag) {
    return static_cast<GpuSceneInstanceFlags>(flag);
}

inline constexpr GpuSceneInstanceFlags kGpuSceneKnownInstanceFlags =
    gpuSceneInstanceFlagBit(GpuSceneInstanceFlag::Enabled) |
    gpuSceneInstanceFlagBit(GpuSceneInstanceFlag::DynamicTransform) |
    gpuSceneInstanceFlagBit(GpuSceneInstanceFlag::ShadowCaster) |
    gpuSceneInstanceFlagBit(GpuSceneInstanceFlag::ReflectionVisible) |
    gpuSceneInstanceFlagBit(GpuSceneInstanceFlag::RayTracingVisible) |
    gpuSceneInstanceFlagBit(GpuSceneInstanceFlag::FirstPerson);

/// Tests whether one packed instance participation rule is present.
/// @param flags Complete packed instance mask.
/// @param flag Participation rule to test.
/// @return True when the requested bit is set.
[[nodiscard]] constexpr bool hasGpuSceneInstanceFlag(const GpuSceneInstanceFlags flags,
                                                     const GpuSceneInstanceFlag flag) {
    return (flags & gpuSceneInstanceFlagBit(flag)) != 0u;
}

/// Selects the indexed triangle element width stored by one geometry record.
enum class GpuSceneIndexType : uint32_t { Uint16 = 0u, Uint32 = 1u };

/// Classifies geometry consistently across raster, shadow, reflection, and ray-query paths.
enum class GpuSceneGeometryFlag : uint32_t {
    Opaque = 1u << 0u,
    Cutout = 1u << 1u,
    Transparent = 1u << 2u,
    ShadowCaster = 1u << 3u,
    ReflectionVisible = 1u << 4u,
    RayTracingVisible = 1u << 5u,
    DynamicVertices = 1u << 6u,
    DoubleSided = 1u << 7u
};

using GpuSceneGeometryFlags = uint32_t;

/// Returns the bit corresponding to one geometry classification rule.
/// @param flag Classification rule to encode.
/// @return Unsigned mask containing exactly one rule bit.
[[nodiscard]] constexpr GpuSceneGeometryFlags gpuSceneGeometryFlagBit(const GpuSceneGeometryFlag flag) {
    return static_cast<GpuSceneGeometryFlags>(flag);
}

inline constexpr GpuSceneGeometryFlags kGpuSceneGeometrySurfaceClassMask =
    gpuSceneGeometryFlagBit(GpuSceneGeometryFlag::Opaque) | gpuSceneGeometryFlagBit(GpuSceneGeometryFlag::Cutout) |
    gpuSceneGeometryFlagBit(GpuSceneGeometryFlag::Transparent);
inline constexpr GpuSceneGeometryFlags kGpuSceneKnownGeometryFlags =
    kGpuSceneGeometrySurfaceClassMask | gpuSceneGeometryFlagBit(GpuSceneGeometryFlag::ShadowCaster) |
    gpuSceneGeometryFlagBit(GpuSceneGeometryFlag::ReflectionVisible) |
    gpuSceneGeometryFlagBit(GpuSceneGeometryFlag::RayTracingVisible) |
    gpuSceneGeometryFlagBit(GpuSceneGeometryFlag::DynamicVertices) |
    gpuSceneGeometryFlagBit(GpuSceneGeometryFlag::DoubleSided);

/// Tests whether one packed geometry classification rule is present.
/// @param flags Complete packed geometry mask.
/// @param flag Classification rule to test.
/// @return True when the requested bit is set.
[[nodiscard]] constexpr bool hasGpuSceneGeometryFlag(const GpuSceneGeometryFlags flags,
                                                     const GpuSceneGeometryFlag flag) {
    return (flags & gpuSceneGeometryFlagBit(flag)) != 0u;
}

/// Defines the immutable 192-byte CPU/GPU instance record used by culling, raster, motion, and ray paths.
struct alignas(16) GpuSceneInstance final {
    /// Current camera-relative affine transform from object to world space.
    GpuSceneAffineTransform worldFromObject;
    /// Previous camera-relative affine transform used for motion reconstruction.
    GpuSceneAffineTransform previousWorldFromObject;
    /// Current inverse affine transform used by ray-hit attribute reconstruction.
    GpuSceneAffineTransform objectFromWorld;
    /// Camera-relative world-space bounding-sphere center and positive radius.
    glm::vec4 worldBoundsCenterAndRadius{0.0f};
    /// Geometry base/count, material-table base, and GpuSceneInstanceFlags.
    glm::uvec4 geometryMaterialAndFlags{0u};
    /// Stable object ID, 24-bit ray-tracing custom index, contract version, and reserved zero.
    glm::uvec4 identityAndVersion{0u, kGpuSceneInvalidRayTracingInstanceId, kGpuSceneContractVersion, 0u};
};

/// Defines the immutable 128-byte CPU/GPU indexed-triangle record shared by raster and ray paths.
struct alignas(16) GpuSceneGeometry final {
    GpuSceneDeviceAddress vertexAddress;
    GpuSceneDeviceAddress indexAddress;
    GpuSceneDeviceAddress primitiveMetadataAddress;
    GpuSceneDeviceAddress meshletAddress;
    /// Vertex stride, position byte offset, vertex count, and GpuSceneGeometryFlags.
    glm::uvec4 vertexLayoutAndFlags{0u};
    /// First index, index count, GpuSceneIndexType, and contract version.
    glm::uvec4 indexRangeAndType{0u, 0u, static_cast<uint32_t>(GpuSceneIndexType::Uint16), kGpuSceneContractVersion};
    /// Asset-relative material index, stable material ID, stable geometry ID, and metadata stride.
    glm::uvec4 materialAndIdentity{0u};
    /// Primitive count, first meshlet, meshlet count, and monotonic geometry revision.
    glm::uvec4 primitiveMeshletAndRevision{0u};
    /// Object-space AABB minimum; w is reserved and must be zero.
    glm::vec4 localBoundsMin{0.0f};
    /// Object-space AABB maximum; w is reserved and must be zero.
    glm::vec4 localBoundsMax{0.0f};
};

/// Carries one visible scene instance into strict fixed-layout normalization.
struct GpuSceneInstanceNormalizationInput final {
    glm::mat4 worldFromObject{1.0f};
    glm::mat4 previousWorldFromObject{1.0f};
    glm::vec4 worldBoundsCenterAndRadius{0.0f};
    uint32_t geometryBase = 0u;
    uint32_t geometryCount = 0u;
    uint32_t materialBase = 0u;
    GpuSceneInstanceFlags flags = 0u;
    StableObjectId stableObjectId;
    uint32_t rayTracingInstanceId = kGpuSceneInvalidRayTracingInstanceId;
};

/// Carries one resident indexed-triangle asset primitive into fixed-layout normalization.
struct GpuSceneGeometryNormalizationInput final {
    uint64_t vertexAddress = 0u;
    uint64_t indexAddress = 0u;
    uint64_t primitiveMetadataAddress = 0u;
    uint64_t meshletAddress = 0u;
    uint32_t vertexStride = 0u;
    uint32_t positionByteOffset = 0u;
    uint32_t vertexCount = 0u;
    uint32_t firstIndex = 0u;
    uint32_t indexCount = 0u;
    GpuSceneIndexType indexType = GpuSceneIndexType::Uint16;
    uint32_t materialIndex = kGpuSceneInvalidTableIndex;
    StableMaterialId stableMaterialId;
    StableGeometryId stableGeometryId;
    uint32_t primitiveMetadataStride = 0u;
    uint32_t firstMeshlet = 0u;
    uint32_t meshletCount = 0u;
    uint32_t geometryRevision = 0u;
    GpuSceneGeometryFlags flags = 0u;
    glm::vec3 localBoundsMin{0.0f};
    glm::vec3 localBoundsMax{0.0f};
};

/// Identifies every deterministic GPU scene normalization failure.
enum class GpuSceneNormalizationError : uint8_t {
    None,
    NonFiniteValue,
    NonAffineTransform,
    SingularTransform,
    ValueOutOfRange,
    InvalidStableId,
    UnknownFlags,
    InvalidGeometryRange,
    InvalidBounds,
    InvalidRayTracingInstanceId,
    RayTracingStateConflict,
    InvalidDeviceAddress,
    InvalidVertexLayout,
    InvalidIndexRange,
    InvalidMaterialIndex,
    InvalidPrimitiveMetadata,
    InvalidMeshletRange
};

/// Identifies the semantic field associated with one GPU scene failure.
enum class GpuSceneField : uint8_t {
    None,
    WorldFromObject,
    PreviousWorldFromObject,
    WorldBounds,
    GeometryRange,
    MaterialBase,
    InstanceFlags,
    StableObjectId,
    RayTracingInstanceId,
    VertexAddress,
    IndexAddress,
    PrimitiveMetadataAddress,
    MeshletAddress,
    VertexLayout,
    IndexRange,
    IndexType,
    MaterialIndex,
    StableMaterialId,
    StableGeometryId,
    PrimitiveMetadataStride,
    MeshletRange,
    GeometryRevision,
    GeometryFlags,
    LocalBounds
};

/// Returns one normalized instance record or a stable semantic error and field.
struct GpuSceneInstanceNormalizationResult final {
    GpuSceneInstance instance;
    GpuSceneNormalizationError error = GpuSceneNormalizationError::None;
    GpuSceneField field = GpuSceneField::None;

    /// Reports whether every source value satisfies the instance contract.
    /// @return True only when no normalization error was recorded.
    [[nodiscard]] bool succeeded() const;
};

/// Returns one normalized geometry record or a stable semantic error and field.
struct GpuSceneGeometryNormalizationResult final {
    GpuSceneGeometry geometry;
    GpuSceneNormalizationError error = GpuSceneNormalizationError::None;
    GpuSceneField field = GpuSceneField::None;

    /// Reports whether every source value satisfies the geometry contract.
    /// @return True only when no normalization error was recorded.
    [[nodiscard]] bool succeeded() const;
};

/// Validates and packs one visible scene instance into the fixed GPU record.
/// @param input Current/previous transforms, bounds, table ranges, and stable identity.
/// @return Packed instance or a stable field-specific validation error.
[[nodiscard]] GpuSceneInstanceNormalizationResult
normalizeGpuSceneInstance(const GpuSceneInstanceNormalizationInput& input);

/// Validates and packs one resident indexed-triangle geometry into the fixed GPU record.
/// @param input Device addresses, element ranges, material identity, bounds, and revision.
/// @return Packed geometry or a stable field-specific validation error.
[[nodiscard]] GpuSceneGeometryNormalizationResult
normalizeGpuSceneGeometry(const GpuSceneGeometryNormalizationInput& input);

/// Transforms one point by the three explicitly stored affine rows.
/// @param transform Fixed GPU affine transform.
/// @param point Object- or world-space point with an implicit homogeneous one.
/// @return Transformed three-component point.
[[nodiscard]] glm::vec3 transformGpuScenePoint(const GpuSceneAffineTransform& transform, const glm::vec3& point);

/// Returns the stable identifier used by logs and tests for one scene normalization error.
/// @param error Error to identify.
/// @return Process-lifetime string containing the stable identifier.
[[nodiscard]] const char* gpuSceneNormalizationErrorStableId(GpuSceneNormalizationError error);

/// Returns the stable identifier used by diagnostics for one scene field.
/// @param field Semantic field to identify.
/// @return Process-lifetime string containing the stable identifier.
[[nodiscard]] const char* gpuSceneFieldStableId(GpuSceneField field);

static_assert(alignof(GpuSceneDeviceAddress) == 8u);
static_assert(sizeof(GpuSceneDeviceAddress) == 8u);
static_assert(alignof(GpuSceneAffineTransform) == 16u);
static_assert(sizeof(GpuSceneAffineTransform) == 48u);
static_assert(alignof(GpuSceneInstance) == 16u);
static_assert(sizeof(GpuSceneInstance) == 192u);
static_assert(offsetof(GpuSceneInstance, worldFromObject) == 0u);
static_assert(offsetof(GpuSceneInstance, previousWorldFromObject) == 48u);
static_assert(offsetof(GpuSceneInstance, objectFromWorld) == 96u);
static_assert(offsetof(GpuSceneInstance, worldBoundsCenterAndRadius) == 144u);
static_assert(offsetof(GpuSceneInstance, geometryMaterialAndFlags) == 160u);
static_assert(offsetof(GpuSceneInstance, identityAndVersion) == 176u);
static_assert(alignof(GpuSceneGeometry) == 16u);
static_assert(sizeof(GpuSceneGeometry) == 128u);
static_assert(offsetof(GpuSceneGeometry, vertexAddress) == 0u);
static_assert(offsetof(GpuSceneGeometry, indexAddress) == 8u);
static_assert(offsetof(GpuSceneGeometry, primitiveMetadataAddress) == 16u);
static_assert(offsetof(GpuSceneGeometry, meshletAddress) == 24u);
static_assert(offsetof(GpuSceneGeometry, vertexLayoutAndFlags) == 32u);
static_assert(offsetof(GpuSceneGeometry, indexRangeAndType) == 48u);
static_assert(offsetof(GpuSceneGeometry, materialAndIdentity) == 64u);
static_assert(offsetof(GpuSceneGeometry, primitiveMeshletAndRevision) == 80u);
static_assert(offsetof(GpuSceneGeometry, localBoundsMin) == 96u);
static_assert(offsetof(GpuSceneGeometry, localBoundsMax) == 112u);
static_assert(std::is_standard_layout_v<GpuSceneDeviceAddress>);
static_assert(std::is_standard_layout_v<GpuSceneAffineTransform>);
static_assert(std::is_standard_layout_v<GpuSceneInstance>);
static_assert(std::is_standard_layout_v<GpuSceneGeometry>);
static_assert(std::is_trivially_copyable_v<GpuSceneDeviceAddress>);
static_assert(std::is_trivially_copyable_v<GpuSceneAffineTransform>);
static_assert(std::is_trivially_copyable_v<GpuSceneInstance>);
static_assert(std::is_trivially_copyable_v<GpuSceneGeometry>);

} // namespace renderer::contracts

#endif // MECRAFT_GPU_SCENE_CONTRACT_H
