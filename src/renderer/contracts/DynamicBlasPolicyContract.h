#ifndef MECRAFT_DYNAMIC_BLAS_POLICY_CONTRACT_H
#define MECRAFT_DYNAMIC_BLAS_POLICY_CONTRACT_H

#include "renderer/rhi/RhiTypes.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace renderer::contracts {

/// Maximum number of consecutive in-place updates before a full build restores BVH quality.
inline constexpr uint32_t kDynamicBlasMaximumConsecutiveUpdates = 120u;

/// Classifies the current vertex positions relative to a retained rigid-reference mesh.
enum class DynamicBlasPoseRelation : uint8_t { NonRigid = 0u, BitwiseEqual = 1u, YawTranslation = 2u };

/// Selects the acceleration-structure operation for one dynamic geometry snapshot.
enum class DynamicBlasAction : uint8_t { RigidReuse = 0u, Update = 1u, Rebuild = 2u, Count = 3u };

/// Identifies why the retained rigid-reference BLAS cannot represent the current snapshot.
enum class DynamicBlasRigidReuseRejectReason : uint8_t {
    None = 0u,
    MissingReference,
    BuildFlagsChanged,
    GeometryCountChanged,
    GeometryClassChanged,
    GeometryFlagsChanged,
    VertexFormatChanged,
    VertexStrideChanged,
    IndexFormatChanged,
    VertexCountChanged,
    PrimitiveCountChanged,
    IndexCountChanged,
    IndexTopologyChanged,
    NonRigidPose,
    ShadingChanged,
    Count
};

/// Identifies why an existing dynamic BLAS slot requires a full build instead of UPDATE mode.
enum class DynamicBlasUpdateRejectReason : uint8_t {
    None = 0u,
    UpdatesDisabled,
    MissingSlot,
    UpdateNotAllowed,
    BuildFlagsChanged,
    GeometryCountChanged,
    GeometryClassChanged,
    GeometryFlagsChanged,
    VertexFormatChanged,
    VertexStrideChanged,
    IndexFormatChanged,
    VertexCountChanged,
    PrimitiveCountChanged,
    IndexCountChanged,
    IndexTopologyChanged,
    PeriodicRebuild,
    Count
};

/// Stores every Vulkan UPDATE-compatibility field for one ordered BLAS geometry bucket.
struct DynamicBlasGeometrySignature final {
    uint32_t geometryClass = 0u;
    RhiAccelerationStructureGeometryFlags geometryFlags = 0u;
    RhiVertexFormat vertexFormat = RhiVertexFormat::Float3;
    uint32_t vertexStride = 0u;
    RhiAccelerationStructureIndexFormat indexFormat = RhiAccelerationStructureIndexFormat::None;
    uint32_t vertexCount = 0u;
    uint32_t primitiveCount = 0u;
    uint32_t indexCount = 0u;
    uint64_t indexTopologyHash = 0u;
};

/// Captures one complete dynamic BLAS build signature and its external shading identity.
struct DynamicBlasBuildSignature final {
    RhiAccelerationStructureBuildFlags buildFlags = 0u;
    uint64_t shadingHash = 0u;
    std::vector<DynamicBlasGeometrySignature> geometries;
};

/// Supplies the retained rigid reference, selected update slot, and current dynamic mesh state.
struct DynamicBlasPolicyInput final {
    DynamicBlasBuildSignature current;
    std::optional<DynamicBlasBuildSignature> rigidReference;
    DynamicBlasPoseRelation poseRelation = DynamicBlasPoseRelation::NonRigid;
    bool updatesEnabled = false;
    std::optional<DynamicBlasBuildSignature> updateSlot;
    uint32_t consecutiveUpdates = 0u;
};

/// Reports the selected operation and both ordered eligibility checks.
struct DynamicBlasPolicyDecision final {
    DynamicBlasAction action = DynamicBlasAction::Rebuild;
    DynamicBlasRigidReuseRejectReason rigidReuseReject = DynamicBlasRigidReuseRejectReason::None;
    DynamicBlasUpdateRejectReason updateReject = DynamicBlasUpdateRejectReason::None;
};

/// Validates every field required to issue a deterministic BLAS BUILD or UPDATE command.
/// @param signature Ordered geometry buckets, build flags, and external shading identity.
/// @return True when all geometry ranges and hashes satisfy the current RHI contract.
[[nodiscard]] bool validDynamicBlasBuildSignature(const DynamicBlasBuildSignature& signature);

/// Evaluates rigid transform reuse first, then exact Vulkan UPDATE compatibility.
/// @param input Current mesh plus the independently retained rigid reference and selected update slot.
/// @return A deterministic action and rejection reasons, or no value for an internally inconsistent input.
[[nodiscard]] std::optional<DynamicBlasPolicyDecision> evaluateDynamicBlasPolicy(const DynamicBlasPolicyInput& input);

/// Returns the stable report identifier for one dynamic BLAS action.
[[nodiscard]] const char* dynamicBlasActionStableId(DynamicBlasAction action);

/// Returns the stable report identifier for one rigid-reuse rejection reason.
[[nodiscard]] const char* dynamicBlasRigidReuseRejectStableId(DynamicBlasRigidReuseRejectReason reason);

/// Returns the stable report identifier for one UPDATE rejection reason.
[[nodiscard]] const char* dynamicBlasUpdateRejectStableId(DynamicBlasUpdateRejectReason reason);

} // namespace renderer::contracts

#endif // MECRAFT_DYNAMIC_BLAS_POLICY_CONTRACT_H
