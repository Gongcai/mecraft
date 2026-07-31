#include "ReflectionProbeContract.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <utility>

namespace renderer::contracts {
namespace {

[[nodiscard]] bool finite(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

[[nodiscard]] bool orderedBounds(const glm::vec3& minimum,
                                 const glm::vec3& maximum) {
    return glm::all(glm::lessThan(minimum, maximum));
}

[[nodiscard]] bool containsInclusive(const glm::vec3& minimum,
                                     const glm::vec3& maximum,
                                     const glm::vec3& point) {
    return glm::all(glm::lessThanEqual(minimum, point)) &&
           glm::all(glm::lessThanEqual(point, maximum));
}

[[nodiscard]] bool containsStrict(const glm::vec3& minimum,
                                  const glm::vec3& maximum,
                                  const glm::vec3& point) {
    return glm::all(glm::lessThan(minimum, point)) &&
           glm::all(glm::lessThan(point, maximum));
}

[[nodiscard]] ReflectionProbeNormalizationResult normalizationFailure(
    const ReflectionProbeError error,
    const ReflectionProbeField field) {
    ReflectionProbeNormalizationResult result;
    result.error = error;
    result.field = field;
    return result;
}

[[nodiscard]] ReflectionProbeValidationResult validationFailure(
    const ReflectionProbeError error,
    const ReflectionProbeField field) {
    ReflectionProbeValidationResult result;
    result.error = error;
    result.field = field;
    return result;
}

[[nodiscard]] ReflectionProbeValidationResult validateValues(
    const StableReflectionProbeId probeId,
    const glm::vec3& position,
    const float exposure,
    const glm::vec3& influenceMinimum,
    const glm::vec3& influenceMaximum,
    const float blendDistance,
    const glm::vec3& projectionMinimum,
    const glm::vec3& projectionMaximum,
    const float validity,
    const uint32_t cubemapIndex,
    const uint32_t captureRevision) {
    if (!probeId.isValid()) {
        return validationFailure(ReflectionProbeError::InvalidStableId,
                                 ReflectionProbeField::StableId);
    }
    if (!finite(position)) {
        return validationFailure(ReflectionProbeError::NonFiniteValue,
                                 ReflectionProbeField::Position);
    }
    if (!std::isfinite(exposure)) {
        return validationFailure(ReflectionProbeError::NonFiniteValue,
                                 ReflectionProbeField::Exposure);
    }
    if (exposure <= 0.0f) {
        return validationFailure(ReflectionProbeError::ValueOutOfRange,
                                 ReflectionProbeField::Exposure);
    }
    if (!finite(influenceMinimum) || !finite(influenceMaximum)) {
        return validationFailure(ReflectionProbeError::NonFiniteValue,
                                 ReflectionProbeField::InfluenceBounds);
    }
    if (!orderedBounds(influenceMinimum, influenceMaximum)) {
        return validationFailure(ReflectionProbeError::InvalidInfluenceBounds,
                                 ReflectionProbeField::InfluenceBounds);
    }
    if (!containsStrict(influenceMinimum, influenceMaximum, position)) {
        return validationFailure(
            ReflectionProbeError::PositionOutsideInfluenceBounds,
            ReflectionProbeField::Position);
    }
    if (!std::isfinite(blendDistance)) {
        return validationFailure(ReflectionProbeError::NonFiniteValue,
                                 ReflectionProbeField::BlendDistance);
    }
    const glm::vec3 influenceExtent =
        influenceMaximum - influenceMinimum;
    const float maximumBlendDistance =
        0.5f * std::min({influenceExtent.x, influenceExtent.y,
                         influenceExtent.z});
    if (blendDistance <= 0.0f || blendDistance > maximumBlendDistance) {
        return validationFailure(ReflectionProbeError::InvalidBlendDistance,
                                 ReflectionProbeField::BlendDistance);
    }
    if (!finite(projectionMinimum) || !finite(projectionMaximum)) {
        return validationFailure(ReflectionProbeError::NonFiniteValue,
                                 ReflectionProbeField::BoxProjectionBounds);
    }
    if (!orderedBounds(projectionMinimum, projectionMaximum)) {
        return validationFailure(
            ReflectionProbeError::InvalidBoxProjectionBounds,
            ReflectionProbeField::BoxProjectionBounds);
    }
    if (!containsInclusive(projectionMinimum, projectionMaximum,
                           influenceMinimum) ||
        !containsInclusive(projectionMinimum, projectionMaximum,
                           influenceMaximum)) {
        return validationFailure(
            ReflectionProbeError::InfluenceOutsideBoxProjectionBounds,
            ReflectionProbeField::BoxProjectionBounds);
    }
    if (!std::isfinite(validity)) {
        return validationFailure(ReflectionProbeError::NonFiniteValue,
                                 ReflectionProbeField::Validity);
    }
    if (validity < 0.0f || validity > 1.0f) {
        return validationFailure(ReflectionProbeError::ValueOutOfRange,
                                 ReflectionProbeField::Validity);
    }
    const bool captured = validity > 0.0f;
    if (captured != (cubemapIndex != kReflectionProbeInvalidCubemapIndex)) {
        return validationFailure(ReflectionProbeError::CaptureStateConflict,
                                 ReflectionProbeField::PrefilteredCubemapIndex);
    }
    if (captured != (captureRevision != 0u)) {
        return validationFailure(ReflectionProbeError::CaptureStateConflict,
                                 ReflectionProbeField::CaptureRevision);
    }
    return {};
}

[[nodiscard]] ReflectionProbeSelectionResult selectionFailure(
    const ReflectionProbeError error,
    const ReflectionProbeField field,
    const uint32_t probeIndex,
    const StableReflectionProbeId probeId) {
    ReflectionProbeSelectionResult result;
    result.error = error;
    result.field = field;
    result.probeIndex = probeIndex;
    result.probeId = probeId;
    return result;
}

[[nodiscard]] ReflectionProbeGridBuildResult gridFailure(
    const ReflectionProbeGridError error,
    const uint32_t sourceProbeIndex,
    const StableReflectionProbeId probeId,
    const ReflectionProbeError probeError = ReflectionProbeError::None,
    const ReflectionProbeField probeField = ReflectionProbeField::None) {
    ReflectionProbeGridBuildResult result;
    result.error = error;
    result.probeError = probeError;
    result.probeField = probeField;
    result.sourceProbeIndex = sourceProbeIndex;
    result.probeId = probeId;
    return result;
}

} // namespace

bool ReflectionProbeNormalizationResult::succeeded() const {
    return error == ReflectionProbeError::None;
}

bool ReflectionProbeValidationResult::succeeded() const {
    return error == ReflectionProbeError::None;
}

bool ReflectionProbeSelectionResult::succeeded() const {
    return error == ReflectionProbeError::None;
}

bool ReflectionProbeGridBuildResult::succeeded() const {
    return error == ReflectionProbeGridError::None;
}

ReflectionProbeNormalizationResult normalizeReflectionProbe(
    const ReflectionProbeNormalizationInput& input) {
    const ReflectionProbeValidationResult validation = validateValues(
        input.probeId, input.positionMeters, input.exposureScale,
        input.influenceMinMeters, input.influenceMaxMeters,
        input.blendDistanceMeters, input.boxProjectionMinMeters,
        input.boxProjectionMaxMeters, input.validity,
        input.prefilteredCubemapIndex, input.captureRevision);
    if (!validation.succeeded()) {
        return normalizationFailure(validation.error, validation.field);
    }

    ReflectionProbeNormalizationResult result;
    result.probe.positionAndExposure =
        glm::vec4(input.positionMeters, input.exposureScale);
    result.probe.influenceMinAndBlendDistance =
        glm::vec4(input.influenceMinMeters, input.blendDistanceMeters);
    result.probe.influenceMaxAndValidity =
        glm::vec4(input.influenceMaxMeters, input.validity);
    result.probe.boxProjectionMin =
        glm::vec4(input.boxProjectionMinMeters, 0.0f);
    result.probe.boxProjectionMax =
        glm::vec4(input.boxProjectionMaxMeters, 0.0f);
    result.probe.resourcesAndIdentity = {
        input.prefilteredCubemapIndex, input.probeId.value,
        input.captureRevision, kReflectionProbeContractVersion};
    return result;
}

ReflectionProbeValidationResult validateReflectionProbe(
    const GpuReflectionProbe& probe) {
    if (probe.resourcesAndIdentity.w != kReflectionProbeContractVersion) {
        return validationFailure(ReflectionProbeError::InvalidContractVersion,
                                 ReflectionProbeField::ContractVersion);
    }
    if (probe.boxProjectionMin.w != 0.0f ||
        probe.boxProjectionMax.w != 0.0f) {
        return validationFailure(ReflectionProbeError::NonZeroReservedValue,
                                 ReflectionProbeField::ReservedValue);
    }
    return validateValues(
        StableReflectionProbeId{probe.resourcesAndIdentity.y},
        glm::vec3(probe.positionAndExposure), probe.positionAndExposure.w,
        glm::vec3(probe.influenceMinAndBlendDistance),
        glm::vec3(probe.influenceMaxAndValidity),
        probe.influenceMinAndBlendDistance.w,
        glm::vec3(probe.boxProjectionMin),
        glm::vec3(probe.boxProjectionMax),
        probe.influenceMaxAndValidity.w, probe.resourcesAndIdentity.x,
        probe.resourcesAndIdentity.z);
}

std::optional<float> reflectionProbeInfluenceWeight(
    const GpuReflectionProbe& probe,
    const glm::vec3& surfacePosition,
    const glm::vec3& surfaceNormal) {
    if (!validateReflectionProbe(probe).succeeded() ||
        !finite(surfacePosition) || !finite(surfaceNormal)) {
        return std::nullopt;
    }
    const float normalLengthSquared = glm::dot(surfaceNormal, surfaceNormal);
    if (!std::isfinite(normalLengthSquared) || normalLengthSquared <= 0.0f) {
        return std::nullopt;
    }

    const glm::vec3 influenceMinimum(
        probe.influenceMinAndBlendDistance);
    const glm::vec3 influenceMaximum(probe.influenceMaxAndValidity);
    if (!containsInclusive(influenceMinimum, influenceMaximum,
                           surfacePosition)) {
        return 0.0f;
    }

    const glm::vec3 distanceToBoundary = glm::min(
        surfacePosition - influenceMinimum,
        influenceMaximum - surfacePosition);
    const float boundaryDistance = std::min(
        {distanceToBoundary.x, distanceToBoundary.y,
         distanceToBoundary.z});
    const float boundaryWeight = std::clamp(
        boundaryDistance / probe.influenceMinAndBlendDistance.w,
        0.0f, 1.0f);

    const glm::vec3 probePosition(probe.positionAndExposure);
    const glm::vec3 probeOffset = surfacePosition - probePosition;
    const glm::vec3 directionalExtent{
        probeOffset.x >= 0.0f
            ? influenceMaximum.x - probePosition.x
            : probePosition.x - influenceMinimum.x,
        probeOffset.y >= 0.0f
            ? influenceMaximum.y - probePosition.y
            : probePosition.y - influenceMinimum.y,
        probeOffset.z >= 0.0f
            ? influenceMaximum.z - probePosition.z
            : probePosition.z - influenceMinimum.z};
    const glm::vec3 normalizedOffset = glm::abs(probeOffset) /
        directionalExtent;
    const float normalizedDistance = std::max(
        {normalizedOffset.x, normalizedOffset.y, normalizedOffset.z});
    const float distanceWeight =
        1.0f - std::clamp(normalizedDistance, 0.0f, 1.0f);

    const float probeDistanceSquared = glm::dot(probeOffset, probeOffset);
    float facingWeight = 1.0f;
    if (probeDistanceSquared > 0.0f) {
        const glm::vec3 normalizedSurfaceNormal =
            surfaceNormal / std::sqrt(normalLengthSquared);
        const glm::vec3 surfaceToProbe =
            -probeOffset / std::sqrt(probeDistanceSquared);
        facingWeight = std::max(
            glm::dot(normalizedSurfaceNormal, surfaceToProbe), 0.0f);
    }

    return std::clamp(boundaryWeight * distanceWeight * facingWeight *
                          probe.influenceMaxAndValidity.w,
                      0.0f, 1.0f);
}

ReflectionProbeSelectionResult selectReflectionProbes(
    const std::vector<GpuReflectionProbe>& probes,
    const glm::vec3& surfacePosition,
    const glm::vec3& surfaceNormal) {
    if (!finite(surfacePosition)) {
        return selectionFailure(ReflectionProbeError::NonFiniteValue,
                                ReflectionProbeField::SurfacePosition,
                                0u, {});
    }
    if (!finite(surfaceNormal)) {
        return selectionFailure(ReflectionProbeError::NonFiniteValue,
                                ReflectionProbeField::SurfaceNormal,
                                0u, {});
    }
    const float normalLengthSquared = glm::dot(surfaceNormal, surfaceNormal);
    if (!std::isfinite(normalLengthSquared) || normalLengthSquared <= 0.0f) {
        return selectionFailure(ReflectionProbeError::InvalidSurfaceNormal,
                                ReflectionProbeField::SurfaceNormal,
                                0u, {});
    }
    if (probes.size() > std::numeric_limits<uint32_t>::max()) {
        return selectionFailure(ReflectionProbeError::ValueOutOfRange,
                                ReflectionProbeField::StableId,
                                0u, {});
    }

    std::unordered_set<uint32_t> stableIds;
    stableIds.reserve(probes.size());
    std::vector<ReflectionProbeSelectionEntry> candidates;
    candidates.reserve(probes.size());
    for (uint32_t index = 0u;
         index < static_cast<uint32_t>(probes.size()); ++index) {
        const GpuReflectionProbe& probe = probes[index];
        const StableReflectionProbeId probeId{
            probe.resourcesAndIdentity.y};
        const ReflectionProbeValidationResult validation =
            validateReflectionProbe(probe);
        if (!validation.succeeded()) {
            return selectionFailure(validation.error, validation.field,
                                    index, probeId);
        }
        if (!stableIds.insert(probeId.value).second) {
            return selectionFailure(ReflectionProbeError::DuplicateStableId,
                                    ReflectionProbeField::StableId,
                                    index, probeId);
        }
        const std::optional<float> weight =
            reflectionProbeInfluenceWeight(
                probe, surfacePosition, surfaceNormal);
        if (!weight.has_value()) {
            return selectionFailure(ReflectionProbeError::NonFiniteValue,
                                    ReflectionProbeField::SurfacePosition,
                                    index, probeId);
        }
        if (*weight > 0.0f) {
            candidates.push_back({index, probeId, *weight});
        }
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const ReflectionProbeSelectionEntry& lhs,
                 const ReflectionProbeSelectionEntry& rhs) {
                  if (lhs.weight != rhs.weight) {
                      return lhs.weight > rhs.weight;
                  }
                  return lhs.probeId.value < rhs.probeId.value;
              });

    ReflectionProbeSelectionResult result;
    result.selection.count = std::min(
        static_cast<uint32_t>(candidates.size()),
        kReflectionProbeBlendCount);
    float totalWeight = 0.0f;
    for (uint32_t index = 0u; index < result.selection.count; ++index) {
        result.selection.entries[index] = candidates[index];
        totalWeight += candidates[index].weight;
    }
    if (totalWeight > 0.0f) {
        for (uint32_t index = 0u; index < result.selection.count; ++index) {
            result.selection.entries[index].weight /= totalWeight;
        }
    }
    return result;
}

ReflectionProbeGridBuildResult buildReflectionProbeGrid(
    const std::vector<GpuReflectionProbe>& probes) {
    if (probes.size() > kReflectionProbeGridMaxProbeCount) {
        return gridFailure(ReflectionProbeGridError::ProbeCapacityExceeded,
                           0u, {});
    }

    struct ActiveProbe final {
        uint32_t sourceIndex = 0u;
        StableReflectionProbeId probeId;
        GpuReflectionProbe probe;
    };
    std::unordered_set<uint32_t> stableIds;
    stableIds.reserve(probes.size());
    std::vector<ActiveProbe> active;
    active.reserve(probes.size());
    for (uint32_t index = 0u;
         index < static_cast<uint32_t>(probes.size()); ++index) {
        const GpuReflectionProbe& probe = probes[index];
        const StableReflectionProbeId probeId{
            probe.resourcesAndIdentity.y};
        const ReflectionProbeValidationResult validation =
            validateReflectionProbe(probe);
        if (!validation.succeeded()) {
            return gridFailure(ReflectionProbeGridError::InvalidProbe,
                               index, probeId, validation.error,
                               validation.field);
        }
        if (!stableIds.insert(probeId.value).second) {
            return gridFailure(ReflectionProbeGridError::DuplicateStableId,
                               index, probeId);
        }
        if (probe.influenceMaxAndValidity.w > 0.0f) {
            active.push_back({index, probeId, probe});
        }
    }
    std::sort(active.begin(), active.end(),
              [](const ActiveProbe& lhs, const ActiveProbe& rhs) {
                  return lhs.probeId.value < rhs.probeId.value;
              });

    ReflectionProbeGridBuildResult result;
    if (active.empty()) {
        return result;
    }

    glm::vec3 gridMinimum(std::numeric_limits<float>::max());
    glm::vec3 gridMaximum(std::numeric_limits<float>::lowest());
    result.grid.probes.reserve(active.size());
    for (const ActiveProbe& entry : active) {
        gridMinimum = glm::min(
            gridMinimum,
            glm::vec3(entry.probe.influenceMinAndBlendDistance));
        gridMaximum = glm::max(
            gridMaximum,
            glm::vec3(entry.probe.influenceMaxAndValidity));
        result.grid.probes.push_back(entry.probe);
    }

    const float cellSize = kReflectionProbeGridCellSizeMeters;
    const glm::vec3 origin = glm::floor(gridMinimum / cellSize) * cellSize;
    const glm::vec3 dimensionFloat = glm::ceil(
        (gridMaximum - origin) / cellSize);
    if (!finite(origin) || !finite(dimensionFloat) ||
        glm::any(glm::lessThanEqual(dimensionFloat, glm::vec3(0.0f))) ||
        glm::any(glm::greaterThan(
            dimensionFloat,
            glm::vec3(static_cast<float>(
                kReflectionProbeGridMaxDimension))))) {
        return gridFailure(ReflectionProbeGridError::DimensionExceeded,
                           0u, {});
    }
    const glm::uvec3 dimensions(dimensionFloat);
    const uint64_t cellCount64 =
        static_cast<uint64_t>(dimensions.x) * dimensions.y * dimensions.z;
    if (cellCount64 == 0u ||
        cellCount64 > kReflectionProbeGridMaxCellCount) {
        return gridFailure(ReflectionProbeGridError::DimensionExceeded,
                           0u, {});
    }
    const uint32_t cellCount = static_cast<uint32_t>(cellCount64);
    std::vector<std::vector<uint32_t>> cellCandidates(cellCount);

    const auto linearIndex = [dimensions](const glm::uvec3 cell) {
        return cell.x + dimensions.x *
            (cell.y + dimensions.y * cell.z);
    };
    uint64_t indexCount = 0u;
    for (uint32_t probeIndex = 0u;
         probeIndex < static_cast<uint32_t>(result.grid.probes.size());
         ++probeIndex) {
        const GpuReflectionProbe& probe = result.grid.probes[probeIndex];
        const glm::vec3 minimum =
            glm::vec3(probe.influenceMinAndBlendDistance);
        const glm::vec3 maximum =
            glm::vec3(probe.influenceMaxAndValidity);
        const glm::uvec3 minimumCell = glm::uvec3(glm::floor(
            (minimum - origin) / cellSize));
        const glm::vec3 maximumCellFloat = glm::ceil(
            (maximum - origin) / cellSize) - glm::vec3(1.0f);
        const glm::uvec3 maximumCell = glm::min(
            glm::uvec3(glm::max(maximumCellFloat, glm::vec3(0.0f))),
            dimensions - glm::uvec3(1u));
        for (uint32_t z = minimumCell.z; z <= maximumCell.z; ++z) {
            for (uint32_t y = minimumCell.y; y <= maximumCell.y; ++y) {
                for (uint32_t x = minimumCell.x; x <= maximumCell.x; ++x) {
                    std::vector<uint32_t>& candidates =
                        cellCandidates[linearIndex({x, y, z})];
                    if (candidates.size() >=
                        kReflectionProbeGridMaxProbesPerCell) {
                        return gridFailure(
                            ReflectionProbeGridError::CellCapacityExceeded,
                            active[probeIndex].sourceIndex,
                            active[probeIndex].probeId);
                    }
                    candidates.push_back(probeIndex);
                    ++indexCount;
                    if (indexCount > kReflectionProbeGridMaxIndexCount) {
                        return gridFailure(
                            ReflectionProbeGridError::IndexCapacityExceeded,
                            active[probeIndex].sourceIndex,
                            active[probeIndex].probeId);
                    }
                }
            }
        }
    }

    result.grid.cells.resize(cellCount);
    result.grid.probeIndices.reserve(static_cast<size_t>(indexCount));
    for (uint32_t cellIndex = 0u; cellIndex < cellCount; ++cellIndex) {
        const std::vector<uint32_t>& candidates = cellCandidates[cellIndex];
        result.grid.cells[cellIndex].offsetAndCount = {
            static_cast<uint32_t>(result.grid.probeIndices.size()),
            static_cast<uint32_t>(candidates.size())};
        result.grid.probeIndices.insert(result.grid.probeIndices.end(),
                                        candidates.begin(), candidates.end());
    }
    result.grid.metadata.originAndCellSize = glm::vec4(origin, cellSize);
    result.grid.metadata.dimensionsAndProbeCount = {
        dimensions.x, dimensions.y, dimensions.z,
        static_cast<uint32_t>(result.grid.probes.size())};
    result.grid.metadata.cellAndIndexCounts = {
        cellCount, static_cast<uint32_t>(result.grid.probeIndices.size()),
        kReflectionProbeGridMaxProbesPerCell,
        kReflectionProbeContractVersion};
    return result;
}

std::optional<uint32_t> reflectionProbeGridCellIndex(
    const GpuReflectionProbeGridMetadata& metadata,
    const glm::uvec3& cell) {
    const glm::uvec3 dimensions(metadata.dimensionsAndProbeCount);
    if (glm::any(glm::greaterThanEqual(cell, dimensions)) ||
        metadata.cellAndIndexCounts.x == 0u) {
        return std::nullopt;
    }
    const uint64_t index = static_cast<uint64_t>(cell.x) +
        static_cast<uint64_t>(dimensions.x) *
            (static_cast<uint64_t>(cell.y) +
             static_cast<uint64_t>(dimensions.y) * cell.z);
    if (index >= metadata.cellAndIndexCounts.x ||
        index > std::numeric_limits<uint32_t>::max()) {
        return std::nullopt;
    }
    return static_cast<uint32_t>(index);
}

std::optional<glm::vec3> boxProjectReflectionDirection(
    const GpuReflectionProbe& probe,
    const glm::vec3& surfacePosition,
    const glm::vec3& reflectionDirection) {
    if (!validateReflectionProbe(probe).succeeded() ||
        !finite(surfacePosition) || !finite(reflectionDirection)) {
        return std::nullopt;
    }
    const glm::vec3 projectionMinimum(probe.boxProjectionMin);
    const glm::vec3 projectionMaximum(probe.boxProjectionMax);
    if (!containsInclusive(projectionMinimum, projectionMaximum,
                           surfacePosition)) {
        return std::nullopt;
    }
    const float directionLengthSquared =
        glm::dot(reflectionDirection, reflectionDirection);
    if (!std::isfinite(directionLengthSquared) ||
        directionLengthSquared <= 0.0f) {
        return std::nullopt;
    }
    const glm::vec3 direction =
        reflectionDirection / std::sqrt(directionLengthSquared);
    glm::vec3 exitDistances(std::numeric_limits<float>::infinity());
    for (uint32_t axis = 0u; axis < 3u; ++axis) {
        if (direction[axis] > 0.0f) {
            exitDistances[axis] =
                (projectionMaximum[axis] - surfacePosition[axis]) /
                direction[axis];
        } else if (direction[axis] < 0.0f) {
            exitDistances[axis] =
                (projectionMinimum[axis] - surfacePosition[axis]) /
                direction[axis];
        }
    }
    const float exitDistance = std::min(
        {exitDistances.x, exitDistances.y, exitDistances.z});
    if (!std::isfinite(exitDistance) || exitDistance < 0.0f) {
        return std::nullopt;
    }
    const glm::vec3 hitPosition =
        surfacePosition + direction * exitDistance;
    const glm::vec3 corrected =
        hitPosition - glm::vec3(probe.positionAndExposure);
    const float correctedLengthSquared = glm::dot(corrected, corrected);
    if (!std::isfinite(correctedLengthSquared) ||
        correctedLengthSquared <= 0.0f) {
        return std::nullopt;
    }
    return corrected / std::sqrt(correctedLengthSquared);
}

const char* reflectionProbeErrorStableId(const ReflectionProbeError error) {
    switch (error) {
        case ReflectionProbeError::None: return "None";
        case ReflectionProbeError::NonFiniteValue: return "NonFiniteValue";
        case ReflectionProbeError::ValueOutOfRange: return "ValueOutOfRange";
        case ReflectionProbeError::InvalidStableId: return "InvalidStableId";
        case ReflectionProbeError::InvalidInfluenceBounds:
            return "InvalidInfluenceBounds";
        case ReflectionProbeError::PositionOutsideInfluenceBounds:
            return "PositionOutsideInfluenceBounds";
        case ReflectionProbeError::InvalidBlendDistance:
            return "InvalidBlendDistance";
        case ReflectionProbeError::InvalidBoxProjectionBounds:
            return "InvalidBoxProjectionBounds";
        case ReflectionProbeError::InfluenceOutsideBoxProjectionBounds:
            return "InfluenceOutsideBoxProjectionBounds";
        case ReflectionProbeError::CaptureStateConflict:
            return "CaptureStateConflict";
        case ReflectionProbeError::InvalidContractVersion:
            return "InvalidContractVersion";
        case ReflectionProbeError::NonZeroReservedValue:
            return "NonZeroReservedValue";
        case ReflectionProbeError::DuplicateStableId:
            return "DuplicateStableId";
        case ReflectionProbeError::InvalidSurfaceNormal:
            return "InvalidSurfaceNormal";
    }
    return "InvalidReflectionProbeError";
}

const char* reflectionProbeFieldStableId(const ReflectionProbeField field) {
    switch (field) {
        case ReflectionProbeField::None: return "None";
        case ReflectionProbeField::StableId: return "StableId";
        case ReflectionProbeField::Position: return "Position";
        case ReflectionProbeField::Exposure: return "Exposure";
        case ReflectionProbeField::InfluenceBounds:
            return "InfluenceBounds";
        case ReflectionProbeField::BlendDistance: return "BlendDistance";
        case ReflectionProbeField::BoxProjectionBounds:
            return "BoxProjectionBounds";
        case ReflectionProbeField::Validity: return "Validity";
        case ReflectionProbeField::PrefilteredCubemapIndex:
            return "PrefilteredCubemapIndex";
        case ReflectionProbeField::CaptureRevision: return "CaptureRevision";
        case ReflectionProbeField::ContractVersion: return "ContractVersion";
        case ReflectionProbeField::ReservedValue: return "ReservedValue";
        case ReflectionProbeField::SurfacePosition: return "SurfacePosition";
        case ReflectionProbeField::SurfaceNormal: return "SurfaceNormal";
    }
    return "InvalidReflectionProbeField";
}

const char* reflectionProbeGridErrorStableId(
    const ReflectionProbeGridError error) {
    switch (error) {
        case ReflectionProbeGridError::None: return "None";
        case ReflectionProbeGridError::InvalidProbe: return "InvalidProbe";
        case ReflectionProbeGridError::DuplicateStableId:
            return "DuplicateStableId";
        case ReflectionProbeGridError::ProbeCapacityExceeded:
            return "ProbeCapacityExceeded";
        case ReflectionProbeGridError::DimensionExceeded:
            return "DimensionExceeded";
        case ReflectionProbeGridError::CellCapacityExceeded:
            return "CellCapacityExceeded";
        case ReflectionProbeGridError::IndexCapacityExceeded:
            return "IndexCapacityExceeded";
    }
    return "InvalidReflectionProbeGridError";
}

} // namespace renderer::contracts
