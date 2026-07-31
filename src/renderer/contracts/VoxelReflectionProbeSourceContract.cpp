#include "VoxelReflectionProbeSourceContract.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/common.hpp>

namespace renderer::contracts {
namespace {

[[nodiscard]] bool finiteVec3(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool orderedBounds(const glm::vec3& minimum, const glm::vec3& maximum) {
    return minimum.x < maximum.x && minimum.y < maximum.y && minimum.z < maximum.z;
}

} // namespace

VoxelReflectionProbeSourceBuildResult
buildVoxelReflectionProbeSources(const VoxelReflectionProbeSourceBuildInput& input) {
    VoxelReflectionProbeSourceBuildResult result;
    if (!input.firstProbeId.isValid()) {
        result.error = VoxelReflectionProbeSourceBuildError::InvalidStableId;
        return result;
    }
    if (!finiteVec3(input.boundsMinWorldMeters) || !finiteVec3(input.boundsMaxWorldMeters) ||
        !std::isfinite(input.exposureScale)) {
        result.error = VoxelReflectionProbeSourceBuildError::NonFiniteInput;
        return result;
    }
    if (!orderedBounds(input.boundsMinWorldMeters, input.boundsMaxWorldMeters)) {
        result.error = VoxelReflectionProbeSourceBuildError::InvalidBounds;
        return result;
    }
    if (!std::isfinite(input.cellSizeMeters) || input.cellSizeMeters <= 0.0f) {
        result.error = VoxelReflectionProbeSourceBuildError::InvalidCellSize;
        return result;
    }
    if (!std::isfinite(input.boundsPaddingMeters) || input.boundsPaddingMeters < 0.0f) {
        result.error = VoxelReflectionProbeSourceBuildError::InvalidPadding;
        return result;
    }
    if (!std::isfinite(input.exposureScale) || input.exposureScale <= 0.0f) {
        result.error = VoxelReflectionProbeSourceBuildError::InvalidExposure;
        return result;
    }
    if (input.requestedRevision == 0u) {
        result.error = VoxelReflectionProbeSourceBuildError::InvalidRevision;
        return result;
    }

    const glm::vec3 boundsMin = input.boundsMinWorldMeters - glm::vec3(input.boundsPaddingMeters);
    const glm::vec3 boundsMax = input.boundsMaxWorldMeters + glm::vec3(input.boundsPaddingMeters);
    const glm::vec3 extent = boundsMax - boundsMin;
    const glm::vec3 dimensionValues = glm::ceil(extent / input.cellSizeMeters);
    if (!finiteVec3(dimensionValues) || dimensionValues.x < 1.0f || dimensionValues.y < 1.0f ||
        dimensionValues.z < 1.0f || dimensionValues.x > static_cast<float>(kReflectionProbeGridMaxDimension) ||
        dimensionValues.y > static_cast<float>(kReflectionProbeGridMaxDimension) ||
        dimensionValues.z > static_cast<float>(kReflectionProbeGridMaxDimension)) {
        result.error = VoxelReflectionProbeSourceBuildError::DimensionExceeded;
        return result;
    }
    const glm::uvec3 dimensions{static_cast<uint32_t>(dimensionValues.x), static_cast<uint32_t>(dimensionValues.y),
                                static_cast<uint32_t>(dimensionValues.z)};
    const uint64_t sourceCount =
        static_cast<uint64_t>(dimensions.x) * static_cast<uint64_t>(dimensions.y) * static_cast<uint64_t>(dimensions.z);
    if (sourceCount == 0u || sourceCount > kReflectionProbeCaptureMaxProbeCount) {
        result.error = VoxelReflectionProbeSourceBuildError::ProbeCapacityExceeded;
        return result;
    }
    if (static_cast<uint64_t>(input.firstProbeId.value) + sourceCount - 1u >=
        static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
        result.error = VoxelReflectionProbeSourceBuildError::StableIdRangeExceeded;
        return result;
    }

    result.sources.reserve(static_cast<std::size_t>(sourceCount));
    const glm::vec3 cellExtent = extent / glm::vec3(dimensions);
    for (uint32_t z = 0u; z < dimensions.z; ++z) {
        for (uint32_t y = 0u; y < dimensions.y; ++y) {
            for (uint32_t x = 0u; x < dimensions.x; ++x) {
                const glm::vec3 influenceMin = boundsMin + glm::vec3(x, y, z) * cellExtent;
                const glm::vec3 influenceMax{
                    x + 1u == dimensions.x ? boundsMax.x : influenceMin.x + cellExtent.x,
                    y + 1u == dimensions.y ? boundsMax.y : influenceMin.y + cellExtent.y,
                    z + 1u == dimensions.z ? boundsMax.z : influenceMin.z + cellExtent.z,
                };
                VoxelReflectionProbeSource source;
                source.probeId =
                    StableReflectionProbeId{input.firstProbeId.value + static_cast<uint32_t>(result.sources.size())};
                source.positionWorldMeters = (influenceMin + influenceMax) * 0.5f;
                source.exposureScale = input.exposureScale;
                source.influenceMinWorldMeters = influenceMin;
                source.influenceMaxWorldMeters = influenceMax;
                source.blendDistanceMeters = std::min({cellExtent.x, cellExtent.y, cellExtent.z}) * 0.2f;
                source.boxProjectionMinWorldMeters = boundsMin;
                source.boxProjectionMaxWorldMeters = boundsMax;
                source.requestedRevision = input.requestedRevision;

                ReflectionProbeNormalizationInput validation;
                validation.probeId = source.probeId;
                validation.positionMeters = source.positionWorldMeters;
                validation.exposureScale = source.exposureScale;
                validation.influenceMinMeters = source.influenceMinWorldMeters;
                validation.influenceMaxMeters = source.influenceMaxWorldMeters;
                validation.blendDistanceMeters = source.blendDistanceMeters;
                validation.boxProjectionMinMeters = source.boxProjectionMinWorldMeters;
                validation.boxProjectionMaxMeters = source.boxProjectionMaxWorldMeters;
                const ReflectionProbeNormalizationResult normalized = normalizeReflectionProbe(validation);
                if (!normalized.succeeded()) {
                    result.error = VoxelReflectionProbeSourceBuildError::InvalidGeneratedSource;
                    result.errorIndex = static_cast<uint32_t>(result.sources.size());
                    result.sources.clear();
                    return result;
                }
                result.sources.push_back(source);
            }
        }
    }
    return result;
}

const char* voxelReflectionProbeSourceBuildErrorStableId(const VoxelReflectionProbeSourceBuildError error) {
    switch (error) {
    case VoxelReflectionProbeSourceBuildError::None: return "None";
    case VoxelReflectionProbeSourceBuildError::InvalidStableId: return "InvalidStableId";
    case VoxelReflectionProbeSourceBuildError::NonFiniteInput: return "NonFiniteInput";
    case VoxelReflectionProbeSourceBuildError::InvalidBounds: return "InvalidBounds";
    case VoxelReflectionProbeSourceBuildError::InvalidCellSize: return "InvalidCellSize";
    case VoxelReflectionProbeSourceBuildError::InvalidPadding: return "InvalidPadding";
    case VoxelReflectionProbeSourceBuildError::InvalidExposure: return "InvalidExposure";
    case VoxelReflectionProbeSourceBuildError::InvalidRevision: return "InvalidRevision";
    case VoxelReflectionProbeSourceBuildError::DimensionExceeded: return "DimensionExceeded";
    case VoxelReflectionProbeSourceBuildError::ProbeCapacityExceeded: return "ProbeCapacityExceeded";
    case VoxelReflectionProbeSourceBuildError::StableIdRangeExceeded: return "StableIdRangeExceeded";
    case VoxelReflectionProbeSourceBuildError::InvalidGeneratedSource: return "InvalidGeneratedSource";
    default: return "Unknown";
    }
}

} // namespace renderer::contracts
