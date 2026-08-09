#include "renderer/contracts/DynamicBlasPolicyContract.h"

#include <cstdlib>
#include <limits>
#include <unordered_set>

namespace renderer::contracts {
namespace {

constexpr RhiAccelerationStructureBuildFlags kKnownBuildFlags =
    rhiFlag(RhiAccelerationStructureBuildFlag::AllowUpdate) |
    rhiFlag(RhiAccelerationStructureBuildFlag::AllowCompaction) |
    rhiFlag(RhiAccelerationStructureBuildFlag::PreferFastTrace) |
    rhiFlag(RhiAccelerationStructureBuildFlag::PreferFastBuild);
constexpr RhiAccelerationStructureGeometryFlags kKnownGeometryFlags =
    rhiFlag(RhiAccelerationStructureGeometryFlag::Opaque) |
    rhiFlag(RhiAccelerationStructureGeometryFlag::NoDuplicateAnyHitInvocation);

[[nodiscard]] bool validPoseRelation(const DynamicBlasPoseRelation relation) {
    switch (relation) {
    case DynamicBlasPoseRelation::NonRigid:
    case DynamicBlasPoseRelation::BitwiseEqual:
    case DynamicBlasPoseRelation::YawTranslation: return true;
    }
    return false;
}

[[nodiscard]] bool validGeometry(const DynamicBlasGeometrySignature& geometry) {
    if ((geometry.geometryFlags & ~kKnownGeometryFlags) != 0u || geometry.vertexFormat != RhiVertexFormat::Float3 ||
        geometry.vertexStride < sizeof(float) * 3u || geometry.vertexStride % alignof(float) != 0u ||
        geometry.vertexCount == 0u || geometry.primitiveCount == 0u ||
        geometry.primitiveCount > std::numeric_limits<uint32_t>::max() / 3u) {
        return false;
    }

    const uint32_t triangleElementCount = geometry.primitiveCount * 3u;
    switch (geometry.indexFormat) {
    case RhiAccelerationStructureIndexFormat::None:
        return geometry.indexCount == 0u && geometry.indexTopologyHash == 0u &&
               geometry.vertexCount >= triangleElementCount;
    case RhiAccelerationStructureIndexFormat::Uint16:
    case RhiAccelerationStructureIndexFormat::Uint32:
        return geometry.indexCount == triangleElementCount && geometry.indexTopologyHash != 0u;
    }
    return false;
}

template <typename Reason>
[[nodiscard]] Reason signatureMismatch(const DynamicBlasBuildSignature& retained,
                                       const DynamicBlasBuildSignature& current) {
    if (retained.buildFlags != current.buildFlags) {
        return Reason::BuildFlagsChanged;
    }
    if (retained.geometries.size() != current.geometries.size()) {
        return Reason::GeometryCountChanged;
    }
    for (size_t index = 0u; index < current.geometries.size(); ++index) {
        const DynamicBlasGeometrySignature& previous = retained.geometries[index];
        const DynamicBlasGeometrySignature& candidate = current.geometries[index];
        if (previous.geometryClass != candidate.geometryClass) {
            return Reason::GeometryClassChanged;
        }
        if (previous.geometryFlags != candidate.geometryFlags) {
            return Reason::GeometryFlagsChanged;
        }
        if (previous.vertexFormat != candidate.vertexFormat) {
            return Reason::VertexFormatChanged;
        }
        if (previous.vertexStride != candidate.vertexStride) {
            return Reason::VertexStrideChanged;
        }
        if (previous.indexFormat != candidate.indexFormat) {
            return Reason::IndexFormatChanged;
        }
        if (previous.vertexCount != candidate.vertexCount) {
            return Reason::VertexCountChanged;
        }
        if (previous.primitiveCount != candidate.primitiveCount) {
            return Reason::PrimitiveCountChanged;
        }
        if (previous.indexCount != candidate.indexCount) {
            return Reason::IndexCountChanged;
        }
        if (previous.indexTopologyHash != candidate.indexTopologyHash) {
            return Reason::IndexTopologyChanged;
        }
    }
    return Reason::None;
}

[[nodiscard]] DynamicBlasRigidReuseRejectReason evaluateRigidReuse(const DynamicBlasPolicyInput& input) {
    if (!input.rigidReference.has_value()) {
        return DynamicBlasRigidReuseRejectReason::MissingReference;
    }
    const DynamicBlasRigidReuseRejectReason mismatch =
        signatureMismatch<DynamicBlasRigidReuseRejectReason>(*input.rigidReference, input.current);
    if (mismatch != DynamicBlasRigidReuseRejectReason::None) {
        return mismatch;
    }
    if (input.poseRelation == DynamicBlasPoseRelation::NonRigid) {
        return DynamicBlasRigidReuseRejectReason::NonRigidPose;
    }
    if (input.rigidReference->shadingHash != input.current.shadingHash) {
        return DynamicBlasRigidReuseRejectReason::ShadingChanged;
    }
    return DynamicBlasRigidReuseRejectReason::None;
}

[[nodiscard]] DynamicBlasUpdateRejectReason evaluateUpdate(const DynamicBlasPolicyInput& input) {
    if (!input.updatesEnabled) {
        return DynamicBlasUpdateRejectReason::UpdatesDisabled;
    }
    if (!input.updateSlot.has_value()) {
        return DynamicBlasUpdateRejectReason::MissingSlot;
    }
    if ((input.updateSlot->buildFlags & rhiFlag(RhiAccelerationStructureBuildFlag::AllowUpdate)) == 0u) {
        return DynamicBlasUpdateRejectReason::UpdateNotAllowed;
    }
    const DynamicBlasUpdateRejectReason mismatch =
        signatureMismatch<DynamicBlasUpdateRejectReason>(*input.updateSlot, input.current);
    if (mismatch != DynamicBlasUpdateRejectReason::None) {
        return mismatch;
    }
    if (input.consecutiveUpdates >= kDynamicBlasMaximumConsecutiveUpdates) {
        return DynamicBlasUpdateRejectReason::PeriodicRebuild;
    }
    return DynamicBlasUpdateRejectReason::None;
}

} // namespace

bool validDynamicBlasBuildSignature(const DynamicBlasBuildSignature& signature) {
    if (signature.shadingHash == 0u || signature.geometries.empty() ||
        signature.geometries.size() > std::numeric_limits<uint32_t>::max() ||
        (signature.buildFlags & ~kKnownBuildFlags) != 0u ||
        ((signature.buildFlags & rhiFlag(RhiAccelerationStructureBuildFlag::PreferFastTrace)) != 0u &&
         (signature.buildFlags & rhiFlag(RhiAccelerationStructureBuildFlag::PreferFastBuild)) != 0u)) {
        return false;
    }

    std::unordered_set<uint32_t> geometryClasses;
    geometryClasses.reserve(signature.geometries.size());
    for (const DynamicBlasGeometrySignature& geometry : signature.geometries) {
        if (!validGeometry(geometry) || !geometryClasses.insert(geometry.geometryClass).second) {
            return false;
        }
    }
    return true;
}

std::optional<DynamicBlasPolicyDecision> evaluateDynamicBlasPolicy(const DynamicBlasPolicyInput& input) {
    if (!validPoseRelation(input.poseRelation) || !validDynamicBlasBuildSignature(input.current) ||
        (input.rigidReference.has_value() && !validDynamicBlasBuildSignature(*input.rigidReference)) ||
        (input.updateSlot.has_value() && !validDynamicBlasBuildSignature(*input.updateSlot)) ||
        (!input.updateSlot.has_value() && input.consecutiveUpdates != 0u)) {
        return std::nullopt;
    }

    DynamicBlasPolicyDecision decision;
    decision.rigidReuseReject = evaluateRigidReuse(input);
    if (decision.rigidReuseReject == DynamicBlasRigidReuseRejectReason::None) {
        decision.action = DynamicBlasAction::RigidReuse;
        return decision;
    }

    decision.updateReject = evaluateUpdate(input);
    decision.action = decision.updateReject == DynamicBlasUpdateRejectReason::None ? DynamicBlasAction::Update
                                                                                   : DynamicBlasAction::Rebuild;
    return decision;
}

const char* dynamicBlasActionStableId(const DynamicBlasAction action) {
    switch (action) {
    case DynamicBlasAction::RigidReuse: return "RigidReuse";
    case DynamicBlasAction::Update: return "Update";
    case DynamicBlasAction::Rebuild: return "Rebuild";
    case DynamicBlasAction::Count: break;
    }
    std::abort();
}

const char* dynamicBlasRigidReuseRejectStableId(const DynamicBlasRigidReuseRejectReason reason) {
    switch (reason) {
    case DynamicBlasRigidReuseRejectReason::None: return "None";
    case DynamicBlasRigidReuseRejectReason::MissingReference: return "MissingReference";
    case DynamicBlasRigidReuseRejectReason::BuildFlagsChanged: return "BuildFlagsChanged";
    case DynamicBlasRigidReuseRejectReason::GeometryCountChanged: return "GeometryCountChanged";
    case DynamicBlasRigidReuseRejectReason::GeometryClassChanged: return "GeometryClassChanged";
    case DynamicBlasRigidReuseRejectReason::GeometryFlagsChanged: return "GeometryFlagsChanged";
    case DynamicBlasRigidReuseRejectReason::VertexFormatChanged: return "VertexFormatChanged";
    case DynamicBlasRigidReuseRejectReason::VertexStrideChanged: return "VertexStrideChanged";
    case DynamicBlasRigidReuseRejectReason::IndexFormatChanged: return "IndexFormatChanged";
    case DynamicBlasRigidReuseRejectReason::VertexCountChanged: return "VertexCountChanged";
    case DynamicBlasRigidReuseRejectReason::PrimitiveCountChanged: return "PrimitiveCountChanged";
    case DynamicBlasRigidReuseRejectReason::IndexCountChanged: return "IndexCountChanged";
    case DynamicBlasRigidReuseRejectReason::IndexTopologyChanged: return "IndexTopologyChanged";
    case DynamicBlasRigidReuseRejectReason::NonRigidPose: return "NonRigidPose";
    case DynamicBlasRigidReuseRejectReason::ShadingChanged: return "ShadingChanged";
    case DynamicBlasRigidReuseRejectReason::Count: break;
    }
    std::abort();
}

const char* dynamicBlasUpdateRejectStableId(const DynamicBlasUpdateRejectReason reason) {
    switch (reason) {
    case DynamicBlasUpdateRejectReason::None: return "None";
    case DynamicBlasUpdateRejectReason::UpdatesDisabled: return "UpdatesDisabled";
    case DynamicBlasUpdateRejectReason::MissingSlot: return "MissingSlot";
    case DynamicBlasUpdateRejectReason::UpdateNotAllowed: return "UpdateNotAllowed";
    case DynamicBlasUpdateRejectReason::BuildFlagsChanged: return "BuildFlagsChanged";
    case DynamicBlasUpdateRejectReason::GeometryCountChanged: return "GeometryCountChanged";
    case DynamicBlasUpdateRejectReason::GeometryClassChanged: return "GeometryClassChanged";
    case DynamicBlasUpdateRejectReason::GeometryFlagsChanged: return "GeometryFlagsChanged";
    case DynamicBlasUpdateRejectReason::VertexFormatChanged: return "VertexFormatChanged";
    case DynamicBlasUpdateRejectReason::VertexStrideChanged: return "VertexStrideChanged";
    case DynamicBlasUpdateRejectReason::IndexFormatChanged: return "IndexFormatChanged";
    case DynamicBlasUpdateRejectReason::VertexCountChanged: return "VertexCountChanged";
    case DynamicBlasUpdateRejectReason::PrimitiveCountChanged: return "PrimitiveCountChanged";
    case DynamicBlasUpdateRejectReason::IndexCountChanged: return "IndexCountChanged";
    case DynamicBlasUpdateRejectReason::IndexTopologyChanged: return "IndexTopologyChanged";
    case DynamicBlasUpdateRejectReason::PeriodicRebuild: return "PeriodicRebuild";
    case DynamicBlasUpdateRejectReason::Count: break;
    }
    std::abort();
}

} // namespace renderer::contracts
