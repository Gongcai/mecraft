#include "renderer/contracts/DynamicBlasPolicyContract.h"

#include <cstdlib>
#include <iostream>
#include <optional>

namespace {

using namespace renderer::contracts;

[[nodiscard]] bool requireTrue(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[dynamic_blas_policy_contract_test] " << message << '\n';
        return false;
    }
    return true;
}

[[nodiscard]] DynamicBlasBuildSignature makeSignature() {
    DynamicBlasBuildSignature signature;
    signature.buildFlags = rhiFlag(RhiAccelerationStructureBuildFlag::AllowUpdate) |
                           rhiFlag(RhiAccelerationStructureBuildFlag::PreferFastTrace);
    signature.shadingHash = 0x123456789abcdef0ull;
    signature.geometries = {{0u, rhiFlag(RhiAccelerationStructureGeometryFlag::Opaque), RhiVertexFormat::Float3, 32u,
                             RhiAccelerationStructureIndexFormat::Uint32, 40u, 20u, 60u, 0x1111222233334444ull},
                            {1u, 0u, RhiVertexFormat::Float3, 32u, RhiAccelerationStructureIndexFormat::Uint32, 24u, 8u,
                             24u, 0x5555666677778888ull}};
    return signature;
}

[[nodiscard]] bool testSignatureValidation() {
    const DynamicBlasBuildSignature valid = makeSignature();
    DynamicBlasBuildSignature duplicateClass = valid;
    duplicateClass.geometries[1].geometryClass = duplicateClass.geometries[0].geometryClass;
    DynamicBlasBuildSignature invalidTopology = valid;
    invalidTopology.geometries[0].indexTopologyHash = 0u;
    DynamicBlasBuildSignature invalidPreference = valid;
    invalidPreference.buildFlags |= rhiFlag(RhiAccelerationStructureBuildFlag::PreferFastBuild);
    DynamicBlasBuildSignature invalidUnindexed = valid;
    invalidUnindexed.geometries[0].indexFormat = RhiAccelerationStructureIndexFormat::None;

    return requireTrue(validDynamicBlasBuildSignature(valid), "canonical two-bucket signature must be valid") &&
           requireTrue(!validDynamicBlasBuildSignature(duplicateClass),
                       "geometry class identities must remain unique and ordered") &&
           requireTrue(!validDynamicBlasBuildSignature(invalidTopology),
                       "indexed geometry must provide an exact non-zero topology hash") &&
           requireTrue(!validDynamicBlasBuildSignature(invalidPreference),
                       "mutually exclusive build preferences must be rejected") &&
           requireTrue(!validDynamicBlasBuildSignature(invalidUnindexed),
                       "unindexed geometry must not retain indexed range fields");
}

[[nodiscard]] bool testRigidReuse() {
    const DynamicBlasBuildSignature signature = makeSignature();
    DynamicBlasPolicyInput input;
    input.current = signature;
    input.rigidReference = signature;
    input.poseRelation = DynamicBlasPoseRelation::YawTranslation;
    input.updatesEnabled = true;
    input.updateSlot = signature;
    const std::optional<DynamicBlasPolicyDecision> rigid = evaluateDynamicBlasPolicy(input);

    input.current.shadingHash ^= 1u;
    const std::optional<DynamicBlasPolicyDecision> shadingChanged = evaluateDynamicBlasPolicy(input);

    input.current = signature;
    input.current.geometries[1].indexTopologyHash ^= 1u;
    input.updateSlot = input.current;
    const std::optional<DynamicBlasPolicyDecision> topologyChanged = evaluateDynamicBlasPolicy(input);

    return requireTrue(rigid.has_value() && rigid->action == DynamicBlasAction::RigidReuse &&
                           rigid->rigidReuseReject == DynamicBlasRigidReuseRejectReason::None,
                       "exact topology plus fitted yaw and stable shading must reuse the retained BLAS") &&
           requireTrue(shadingChanged.has_value() && shadingChanged->action == DynamicBlasAction::Update &&
                           shadingChanged->rigidReuseReject == DynamicBlasRigidReuseRejectReason::ShadingChanged,
                       "shading changes must leave rigid reuse and enter the compatible full upload path") &&
           requireTrue(topologyChanged.has_value() && topologyChanged->action == DynamicBlasAction::Update &&
                           topologyChanged->rigidReuseReject == DynamicBlasRigidReuseRejectReason::IndexTopologyChanged,
                       "a changed rigid-reference topology may update only a separately compatible slot");
}

[[nodiscard]] bool testUpdateAndRebuild() {
    const DynamicBlasBuildSignature signature = makeSignature();
    DynamicBlasPolicyInput input;
    input.current = signature;
    input.poseRelation = DynamicBlasPoseRelation::NonRigid;
    input.updatesEnabled = true;
    input.updateSlot = signature;
    input.consecutiveUpdates = kDynamicBlasMaximumConsecutiveUpdates - 1u;
    const std::optional<DynamicBlasPolicyDecision> update = evaluateDynamicBlasPolicy(input);

    input.consecutiveUpdates = kDynamicBlasMaximumConsecutiveUpdates;
    const std::optional<DynamicBlasPolicyDecision> periodicBuild = evaluateDynamicBlasPolicy(input);

    input.consecutiveUpdates = 0u;
    input.current.geometries[1].primitiveCount += 1u;
    input.current.geometries[1].indexCount += 3u;
    const std::optional<DynamicBlasPolicyDecision> bucketChanged = evaluateDynamicBlasPolicy(input);

    input.current = signature;
    input.updateSlot->buildFlags &= ~rhiFlag(RhiAccelerationStructureBuildFlag::AllowUpdate);
    input.current.buildFlags = input.updateSlot->buildFlags;
    const std::optional<DynamicBlasPolicyDecision> immutableSlot = evaluateDynamicBlasPolicy(input);

    return requireTrue(update.has_value() && update->action == DynamicBlasAction::Update &&
                           update->rigidReuseReject == DynamicBlasRigidReuseRejectReason::MissingReference &&
                           update->updateReject == DynamicBlasUpdateRejectReason::None,
                       "non-rigid vertices must use UPDATE when every build invariant matches") &&
           requireTrue(periodicBuild.has_value() && periodicBuild->action == DynamicBlasAction::Rebuild &&
                           periodicBuild->updateReject == DynamicBlasUpdateRejectReason::PeriodicRebuild,
                       "the fixed update interval must force a full BVH rebuild") &&
           requireTrue(bucketChanged.has_value() && bucketChanged->action == DynamicBlasAction::Rebuild &&
                           bucketChanged->updateReject == DynamicBlasUpdateRejectReason::PrimitiveCountChanged,
                       "per-bucket primitive counts must remain identical for UPDATE mode") &&
           requireTrue(immutableSlot.has_value() && immutableSlot->action == DynamicBlasAction::Rebuild &&
                           immutableSlot->updateReject == DynamicBlasUpdateRejectReason::UpdateNotAllowed,
                       "a slot built without AllowUpdate must require a full build");
}

[[nodiscard]] bool testInvalidInputAndStableIds() {
    DynamicBlasPolicyInput invalid;
    invalid.current = makeSignature();
    invalid.consecutiveUpdates = 1u;
    return requireTrue(!evaluateDynamicBlasPolicy(invalid).has_value(),
                       "update counters without a selected slot must be rejected") &&
           requireTrue(std::string(dynamicBlasActionStableId(DynamicBlasAction::RigidReuse)) == "RigidReuse" &&
                           std::string(dynamicBlasRigidReuseRejectStableId(
                               DynamicBlasRigidReuseRejectReason::IndexTopologyChanged)) == "IndexTopologyChanged" &&
                           std::string(dynamicBlasUpdateRejectStableId(
                               DynamicBlasUpdateRejectReason::PeriodicRebuild)) == "PeriodicRebuild",
                       "policy report identifiers must remain stable");
}

} // namespace

int main() {
    const bool valid =
        testSignatureValidation() && testRigidReuse() && testUpdateAndRebuild() && testInvalidInputAndStableIds();
    if (valid) {
        std::cout << "[dynamic_blas_policy_contract_test] PASS\n";
    }
    return valid ? EXIT_SUCCESS : EXIT_FAILURE;
}
