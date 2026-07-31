#ifndef MECRAFT_REFLECTION_PROBE_CONTRACT_H
#define MECRAFT_REFLECTION_PROBE_CONTRACT_H

#include "SceneIdentityContract.h"

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>
#include <vector>

namespace renderer::contracts {

inline constexpr uint32_t kReflectionProbeContractVersion = 1u;
inline constexpr uint32_t kReflectionProbeCubeExtent = 128u;
inline constexpr uint32_t kReflectionProbeCubeMipCount = 8u;
inline constexpr uint32_t kReflectionProbeInvalidCubemapIndex =
    std::numeric_limits<uint32_t>::max();
inline constexpr uint32_t kReflectionProbeBlendCount = 4u;
inline constexpr float kReflectionProbeGridCellSizeMeters = 16.0f;
inline constexpr uint32_t kReflectionProbeGridMaxDimension = 128u;
inline constexpr uint32_t kReflectionProbeGridMaxCellCount = 262144u;
inline constexpr uint32_t kReflectionProbeGridMaxProbeCount = 4096u;
inline constexpr uint32_t kReflectionProbeGridMaxProbesPerCell = 16u;
inline constexpr uint32_t kReflectionProbeGridMaxIndexCount = 1048576u;

/// Defines the immutable 96-byte CPU/GPU reflection-probe record consumed by
/// probe selection, box projection, and environment-lighting passes.
struct alignas(16) GpuReflectionProbe final {
    /// Camera-relative capture position in meters and linear exposure scale.
    glm::vec4 positionAndExposure{0.0f};
    /// Influence AABB minimum in meters and inward blend distance in meters.
    glm::vec4 influenceMinAndBlendDistance{0.0f};
    /// Influence AABB maximum in meters and capture validity in [0, 1].
    glm::vec4 influenceMaxAndValidity{0.0f};
    /// Box-projection AABB minimum in meters; w is reserved and must be zero.
    glm::vec4 boxProjectionMin{0.0f};
    /// Box-projection AABB maximum in meters; w is reserved and must be zero.
    glm::vec4 boxProjectionMax{0.0f};
    /// Prefiltered cubemap index, stable ID, capture revision, and version.
    glm::uvec4 resourcesAndIdentity{
        kReflectionProbeInvalidCubemapIndex, 0u, 0u,
        kReflectionProbeContractVersion};
};

/// Carries source reflection-probe values into the fixed GPU representation.
struct ReflectionProbeNormalizationInput final {
    StableReflectionProbeId probeId;
    glm::vec3 positionMeters{0.0f};
    float exposureScale = 1.0f;
    glm::vec3 influenceMinMeters{0.0f};
    glm::vec3 influenceMaxMeters{0.0f};
    float blendDistanceMeters = 0.0f;
    glm::vec3 boxProjectionMinMeters{0.0f};
    glm::vec3 boxProjectionMaxMeters{0.0f};
    float validity = 0.0f;
    uint32_t prefilteredCubemapIndex =
        kReflectionProbeInvalidCubemapIndex;
    uint32_t captureRevision = 0u;
};

/// Identifies every deterministic reflection-probe contract failure.
enum class ReflectionProbeError : uint8_t {
    None,
    NonFiniteValue,
    ValueOutOfRange,
    InvalidStableId,
    InvalidInfluenceBounds,
    PositionOutsideInfluenceBounds,
    InvalidBlendDistance,
    InvalidBoxProjectionBounds,
    InfluenceOutsideBoxProjectionBounds,
    CaptureStateConflict,
    InvalidContractVersion,
    NonZeroReservedValue,
    DuplicateStableId,
    InvalidSurfaceNormal
};

/// Identifies the semantic field associated with one probe failure.
enum class ReflectionProbeField : uint8_t {
    None,
    StableId,
    Position,
    Exposure,
    InfluenceBounds,
    BlendDistance,
    BoxProjectionBounds,
    Validity,
    PrefilteredCubemapIndex,
    CaptureRevision,
    ContractVersion,
    ReservedValue,
    SurfacePosition,
    SurfaceNormal
};

/// Returns one normalized GPU record or a stable semantic error and field.
struct ReflectionProbeNormalizationResult final {
    GpuReflectionProbe probe;
    ReflectionProbeError error = ReflectionProbeError::None;
    ReflectionProbeField field = ReflectionProbeField::None;

    /// Reports whether every source value satisfies the probe contract.
    /// @return True only when no normalization error was recorded.
    [[nodiscard]] bool succeeded() const;
};

/// Reports validation of an already packed GPU probe record.
struct ReflectionProbeValidationResult final {
    ReflectionProbeError error = ReflectionProbeError::None;
    ReflectionProbeField field = ReflectionProbeField::None;

    /// Reports whether the packed record satisfies the current contract.
    /// @return True only when no validation error was recorded.
    [[nodiscard]] bool succeeded() const;
};

/// Identifies one selected probe and its normalized surface blend weight.
struct ReflectionProbeSelectionEntry final {
    uint32_t probeIndex = 0u;
    StableReflectionProbeId probeId;
    float weight = 0.0f;
};

/// Stores the deterministic Top-4 probe selection for one surface.
struct ReflectionProbeSelection final {
    std::array<ReflectionProbeSelectionEntry,
               kReflectionProbeBlendCount> entries{};
    uint32_t count = 0u;
};

/// Returns a complete selection or the exact invalid query/probe record.
struct ReflectionProbeSelectionResult final {
    ReflectionProbeSelection selection;
    ReflectionProbeError error = ReflectionProbeError::None;
    ReflectionProbeField field = ReflectionProbeField::None;
    uint32_t probeIndex = 0u;
    StableReflectionProbeId probeId;

    /// Reports whether validation and selection completed successfully.
    /// @return True only when no query or probe error was recorded.
    [[nodiscard]] bool succeeded() const;
};

/// Defines the fixed metadata record used to locate one camera-relative probe
/// grid from a surface position without scanning the complete probe snapshot.
struct alignas(16) GpuReflectionProbeGridMetadata final {
    /// Grid origin in camera-relative meters and fixed cubic cell size.
    glm::vec4 originAndCellSize{0.0f, 0.0f, 0.0f,
                                kReflectionProbeGridCellSizeMeters};
    /// Grid dimensions in xyz and active packed probe count in w.
    glm::uvec4 dimensionsAndProbeCount{0u};
    /// Cell count, compact index count, per-cell limit, and contract version.
    glm::uvec4 cellAndIndexCounts{
        0u, 0u, kReflectionProbeGridMaxProbesPerCell,
        kReflectionProbeContractVersion};
    /// Reserved for later streaming metadata and required to remain zero.
    glm::uvec4 reserved{0u};
};

/// Locates one contiguous candidate range in the compact probe-index array.
struct alignas(8) GpuReflectionProbeGridCell final {
    glm::uvec2 offsetAndCount{0u};
};

/// Identifies deterministic failures while building the packed spatial grid.
enum class ReflectionProbeGridError : uint8_t {
    None,
    InvalidProbe,
    DuplicateStableId,
    ProbeCapacityExceeded,
    DimensionExceeded,
    CellCapacityExceeded,
    IndexCapacityExceeded
};

/// Stores packed probes and their deterministic spatial acceleration data.
struct ReflectionProbeGrid final {
    GpuReflectionProbeGridMetadata metadata;
    std::vector<GpuReflectionProbe> probes;
    std::vector<GpuReflectionProbeGridCell> cells;
    std::vector<uint32_t> probeIndices;
};

/// Returns a complete grid or the exact source probe/capacity failure.
struct ReflectionProbeGridBuildResult final {
    ReflectionProbeGrid grid;
    ReflectionProbeGridError error = ReflectionProbeGridError::None;
    ReflectionProbeError probeError = ReflectionProbeError::None;
    ReflectionProbeField probeField = ReflectionProbeField::None;
    uint32_t sourceProbeIndex = 0u;
    StableReflectionProbeId probeId;

    /// Reports whether every source probe was packed into the grid.
    /// @return True only when no probe or capacity error was recorded.
    [[nodiscard]] bool succeeded() const;
};

/// Validates source values and constructs the fixed GPU probe record.
/// The influence AABB must lie inside the box-projection AABB, and the capture
/// state must pair zero validity with no resource or positive validity with a
/// valid cubemap and non-zero revision.
/// @param input Fully resolved source probe and capture metadata.
/// @return Packed probe or a stable field-specific validation error.
[[nodiscard]] ReflectionProbeNormalizationResult normalizeReflectionProbe(
    const ReflectionProbeNormalizationInput& input);

/// Validates one packed GPU probe before upload or CPU reference evaluation.
/// @param probe Packed reflection-probe record.
/// @return Stable semantic validation result.
[[nodiscard]] ReflectionProbeValidationResult validateReflectionProbe(
    const GpuReflectionProbe& probe);

/// Evaluates one probe's unnormalized surface influence.
/// Weight combines boundary fade, normalized probe distance, surface-facing
/// direction, and capture validity. Invalid records or queries return no value.
/// @param probe Valid packed probe record.
/// @param surfacePosition Camera-relative surface position in meters.
/// @param surfaceNormal Non-zero surface normal; normalization is performed.
/// @return Weight in [0, 1], or std::nullopt for a contract violation.
[[nodiscard]] std::optional<float> reflectionProbeInfluenceWeight(
    const GpuReflectionProbe& probe,
    const glm::vec3& surfacePosition,
    const glm::vec3& surfaceNormal);

/// Selects and normalizes at most four overlapping probes deterministically.
/// Positive weights sort descending, with stable ID breaking equal-weight ties.
/// @param probes Complete visible probe snapshot in any submission order.
/// @param surfacePosition Camera-relative surface position in meters.
/// @param surfaceNormal Non-zero surface normal; normalization is performed.
/// @return Top-4 selection or the exact query/probe validation failure.
[[nodiscard]] ReflectionProbeSelectionResult selectReflectionProbes(
    const std::vector<GpuReflectionProbe>& probes,
    const glm::vec3& surfacePosition,
    const glm::vec3& surfaceNormal);

/// Builds a deterministic camera-relative spatial grid for captured probes.
/// Zero-validity probes are validated but excluded from GPU storage. Active
/// probes sort by stable ID so cell candidate order does not depend on scene
/// submission order.
/// @param probes Complete visible probe snapshot.
/// @return Packed probes, cells, and compact indices or a structured failure.
[[nodiscard]] ReflectionProbeGridBuildResult buildReflectionProbeGrid(
    const std::vector<GpuReflectionProbe>& probes);

/// Converts one valid cell coordinate to the packed linear cell index.
/// @param metadata Grid metadata returned by buildReflectionProbeGrid().
/// @param cell Zero-based integer cell coordinate.
/// @return Linear cell index, or std::nullopt when the coordinate is outside.
[[nodiscard]] std::optional<uint32_t> reflectionProbeGridCellIndex(
    const GpuReflectionProbeGridMetadata& metadata,
    const glm::uvec3& cell);

/// Returns the stable identifier used by diagnostics for one grid error.
/// @param error Grid construction error to identify.
/// @return Process-lifetime string containing the stable identifier.
[[nodiscard]] const char* reflectionProbeGridErrorStableId(
    ReflectionProbeGridError error);

/// Corrects a reflection direction against one probe's projection AABB.
/// The surface must be inside the projection box and the direction must be
/// finite and non-zero.
/// @param probe Valid packed probe record.
/// @param surfacePosition Camera-relative ray origin in meters.
/// @param reflectionDirection Non-zero world-space reflection direction.
/// @return Unit direction from the probe to the ray-box exit point, or no value
/// when the query violates the contract.
[[nodiscard]] std::optional<glm::vec3> boxProjectReflectionDirection(
    const GpuReflectionProbe& probe,
    const glm::vec3& surfacePosition,
    const glm::vec3& reflectionDirection);

/// Returns the stable identifier used by logs and tests for one error.
/// @param error Error to identify.
/// @return Process-lifetime string containing the stable identifier.
[[nodiscard]] const char* reflectionProbeErrorStableId(
    ReflectionProbeError error);

/// Returns the stable identifier used by diagnostics for one probe field.
/// @param field Semantic field to identify.
/// @return Process-lifetime string containing the stable identifier.
[[nodiscard]] const char* reflectionProbeFieldStableId(
    ReflectionProbeField field);

static_assert(sizeof(GpuReflectionProbe) == 96u);
static_assert(alignof(GpuReflectionProbe) == 16u);
static_assert(std::is_trivially_copyable_v<GpuReflectionProbe>);
static_assert(std::is_standard_layout_v<GpuReflectionProbe>);
static_assert(sizeof(GpuReflectionProbeGridMetadata) == 64u);
static_assert(alignof(GpuReflectionProbeGridMetadata) == 16u);
static_assert(std::is_trivially_copyable_v<GpuReflectionProbeGridMetadata>);
static_assert(std::is_standard_layout_v<GpuReflectionProbeGridMetadata>);
static_assert(sizeof(GpuReflectionProbeGridCell) == 8u);
static_assert(alignof(GpuReflectionProbeGridCell) == 8u);
static_assert(std::is_trivially_copyable_v<GpuReflectionProbeGridCell>);
static_assert(std::is_standard_layout_v<GpuReflectionProbeGridCell>);

} // namespace renderer::contracts

#endif // MECRAFT_REFLECTION_PROBE_CONTRACT_H
