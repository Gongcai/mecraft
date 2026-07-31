#include "renderer/contracts/VoxelReflectionProbeSourceContract.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int fail(const std::string& message) {
    std::cerr << "[voxel_reflection_probe_source_contract_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

} // namespace

int main() {
    using namespace renderer::contracts;

    VoxelReflectionProbeSourceBuildInput input;
    input.firstProbeId = StableReflectionProbeId{100u};
    input.boundsMinWorldMeters = {0.0f, 0.0f, 0.0f};
    input.boundsMaxWorldMeters = {32.0f, 32.0f, 32.0f};
    input.cellSizeMeters = 16.0f;
    input.boundsPaddingMeters = 1.0f;
    input.requestedRevision = 7u;
    const VoxelReflectionProbeSourceBuildResult result = buildVoxelReflectionProbeSources(input);
    if (!result.succeeded() || result.sources.size() != 27u) {
        return fail("deterministic voxel source grid was not generated");
    }
    if (result.sources.front().probeId.value != 100u || result.sources.back().probeId.value != 126u ||
        result.sources.front().requestedRevision != 7u || result.sources.front().positionWorldMeters.x >= 16.0f ||
        result.sources.back().positionWorldMeters.x <= 16.0f) {
        return fail("voxel source order or stable identity changed");
    }

    VoxelReflectionProbeSourceBuildInput invalidRevision = input;
    invalidRevision.requestedRevision = 0u;
    if (buildVoxelReflectionProbeSources(invalidRevision).error !=
        VoxelReflectionProbeSourceBuildError::InvalidRevision) {
        return fail("zero capture revision was accepted");
    }
    VoxelReflectionProbeSourceBuildInput excessiveDimensions = input;
    excessiveDimensions.cellSizeMeters = 0.1f;
    if (buildVoxelReflectionProbeSources(excessiveDimensions).error !=
        VoxelReflectionProbeSourceBuildError::DimensionExceeded) {
        return fail("dimension overflow was accepted");
    }
    VoxelReflectionProbeSourceBuildInput excessiveCapacity = input;
    excessiveCapacity.boundsMaxWorldMeters = {160.0f, 160.0f, 16.0f};
    if (buildVoxelReflectionProbeSources(excessiveCapacity).error !=
        VoxelReflectionProbeSourceBuildError::ProbeCapacityExceeded) {
        return fail("probe capacity overflow was accepted");
    }

    std::cout << "[voxel_reflection_probe_source_contract_test] PASS\n";
    return EXIT_SUCCESS;
}
