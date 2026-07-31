#ifndef MECRAFT_VOXEL_REFLECTION_PROBE_SOURCE_CONTRACT_H
#define MECRAFT_VOXEL_REFLECTION_PROBE_SOURCE_CONTRACT_H

#include "ReflectionProbeContract.h"

#include <glm/vec3.hpp>

#include <cstdint>
#include <vector>

namespace renderer::contracts {

/// Supplies the loaded voxel-region bounds and deterministic capture inputs.
struct VoxelReflectionProbeSourceBuildInput final {
    StableReflectionProbeId firstProbeId;
    glm::vec3 boundsMinWorldMeters{0.0f};
    glm::vec3 boundsMaxWorldMeters{0.0f};
    float cellSizeMeters = kReflectionProbeGridCellSizeMeters;
    float boundsPaddingMeters = 1.0f;
    float exposureScale = 1.0f;
    uint32_t requestedRevision = 0u;
};

/// Carries one world-space voxel source into the capture pass without binding
/// the source builder to RHI resource types.
struct VoxelReflectionProbeSource final {
    StableReflectionProbeId probeId;
    glm::vec3 positionWorldMeters{0.0f};
    float exposureScale = 1.0f;
    glm::vec3 influenceMinWorldMeters{0.0f};
    glm::vec3 influenceMaxWorldMeters{0.0f};
    float blendDistanceMeters = 0.0f;
    glm::vec3 boxProjectionMinWorldMeters{0.0f};
    glm::vec3 boxProjectionMaxWorldMeters{0.0f};
    uint32_t requestedRevision = 0u;
};

enum class VoxelReflectionProbeSourceBuildError : uint8_t {
    None,
    InvalidStableId,
    NonFiniteInput,
    InvalidBounds,
    InvalidCellSize,
    InvalidPadding,
    InvalidExposure,
    InvalidRevision,
    DimensionExceeded,
    ProbeCapacityExceeded,
    StableIdRangeExceeded,
    InvalidGeneratedSource
};

/// Returns the deterministic regular-grid sources covering one loaded voxel region.
struct VoxelReflectionProbeSourceBuildResult final {
    std::vector<VoxelReflectionProbeSource> sources;
    VoxelReflectionProbeSourceBuildError error = VoxelReflectionProbeSourceBuildError::None;
    uint32_t errorIndex = 0u;

    /// Reports whether every source was generated and contract-validated.
    [[nodiscard]] bool succeeded() const { return error == VoxelReflectionProbeSourceBuildError::None; }
};

/// Builds a fixed-order voxel Probe source snapshot.
/// @param input Complete finite region bounds and capture revision.
/// @return Deterministic sources or a field-specific capacity/input error.
[[nodiscard]] VoxelReflectionProbeSourceBuildResult
buildVoxelReflectionProbeSources(const VoxelReflectionProbeSourceBuildInput& input);

/// Returns the stable diagnostic identifier for one source-builder error.
[[nodiscard]] const char* voxelReflectionProbeSourceBuildErrorStableId(VoxelReflectionProbeSourceBuildError error);

} // namespace renderer::contracts

#endif // MECRAFT_VOXEL_REFLECTION_PROBE_SOURCE_CONTRACT_H
