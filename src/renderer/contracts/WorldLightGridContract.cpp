#include "WorldLightGridContract.h"

#include "ClusteredLightingContract.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace renderer::contracts {
namespace {

static_assert(kWorldLightGridMaxIndexCount >= kClusterMaxLightCount);

struct CellLightPair final {
    glm::ivec3 coordinate{0};
    uint32_t lightIndex = 0u;
};

[[nodiscard]] bool finite(const glm::vec4& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::isfinite(value.w);
}

[[nodiscard]] bool validLight(const GpuLight& light) {
    return light.classificationAndIdentity.x <= static_cast<uint32_t>(GpuLightType::Rect) &&
           light.classificationAndIdentity.y != 0u &&
           light.classificationAndIdentity.z <= static_cast<uint32_t>(GpuLightShadowPolicy::RayQuery) &&
           light.resourcesAndFlags.w == kGpuLightContractVersion &&
           (light.resourcesAndFlags.z & ~kGpuLightKnownContributionFlags) == 0u && gpuLightPackedRangeValid(light) &&
           finite(light.positionAndRange) && finite(light.direction) && finite(light.colorAndIntensity) &&
           finite(light.spotCosinesAndRectSize);
}

[[nodiscard]] bool coordinateLess(const glm::ivec3& lhs, const glm::ivec3& rhs) {
    if (lhs.x != rhs.x) {
        return lhs.x < rhs.x;
    }
    if (lhs.y != rhs.y) {
        return lhs.y < rhs.y;
    }
    return lhs.z < rhs.z;
}

[[nodiscard]] bool sameCoordinate(const glm::ivec3& lhs, const glm::ivec3& rhs) {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}

[[nodiscard]] bool cellCoordinate(const float value, int32_t& coordinate) {
    const double cell = std::floor(static_cast<double>(value) / static_cast<double>(kWorldLightGridCellSizeMeters));
    if (!std::isfinite(cell) || cell < static_cast<double>(std::numeric_limits<int32_t>::min()) ||
        cell > static_cast<double>(std::numeric_limits<int32_t>::max())) {
        return false;
    }
    coordinate = static_cast<int32_t>(cell);
    return true;
}

[[nodiscard]] bool sphereIntersectsCell(const glm::vec3& center, const float radiusSquared,
                                        const glm::ivec3& coordinate) {
    const glm::vec3 cellMinimum = glm::vec3(coordinate) * kWorldLightGridCellSizeMeters;
    const glm::vec3 cellMaximum = cellMinimum + glm::vec3(kWorldLightGridCellSizeMeters);
    const glm::vec3 closest = glm::clamp(center, cellMinimum, cellMaximum);
    const glm::vec3 offset = closest - center;
    return glm::dot(offset, offset) <= radiusSquared;
}

[[nodiscard]] WorldLightGridBuildResult failure(const WorldLightGridBuildError error) {
    WorldLightGridBuildResult result;
    result.error = error;
    return result;
}

} // namespace

WorldLightGridBuildResult buildWorldLightGrid(const std::vector<GpuLight>& lights) {
    if (lights.size() > kClusterMaxLightCount) {
        return failure(WorldLightGridBuildError::TooManyLights);
    }

    std::vector<uint32_t> globalIndices;
    std::vector<CellLightPair> pairs;
    globalIndices.reserve(lights.size());
    for (uint32_t lightIndex = 0u; lightIndex < static_cast<uint32_t>(lights.size()); ++lightIndex) {
        const GpuLight& light = lights[lightIndex];
        if (!validLight(light)) {
            return failure(WorldLightGridBuildError::InvalidLight);
        }
        if (light.classificationAndIdentity.x == static_cast<uint32_t>(GpuLightType::Directional)) {
            if (globalIndices.size() + pairs.size() >= kWorldLightGridMaxIndexCount) {
                return failure(WorldLightGridBuildError::IndexCapacityExceeded);
            }
            globalIndices.push_back(lightIndex);
            continue;
        }

        const glm::vec3 center(light.positionAndRange);
        const float radius = light.positionAndRange.w;
        glm::ivec3 minimumCell;
        glm::ivec3 maximumCell;
        for (uint32_t axis = 0u; axis < 3u; ++axis) {
            if (!cellCoordinate(center[axis] - radius, minimumCell[axis]) ||
                !cellCoordinate(center[axis] + radius, maximumCell[axis])) {
                return failure(WorldLightGridBuildError::CoordinateOverflow);
            }
        }
        const uint64_t countX = static_cast<uint64_t>(static_cast<int64_t>(maximumCell.x) - minimumCell.x) + 1u;
        const uint64_t countY = static_cast<uint64_t>(static_cast<int64_t>(maximumCell.y) - minimumCell.y) + 1u;
        const uint64_t countZ = static_cast<uint64_t>(static_cast<int64_t>(maximumCell.z) - minimumCell.z) + 1u;
        if (countX > kWorldLightGridMaxCellsPerLight || countY > kWorldLightGridMaxCellsPerLight ||
            countZ > kWorldLightGridMaxCellsPerLight || countX * countY > kWorldLightGridMaxCellsPerLight ||
            countX * countY * countZ > kWorldLightGridMaxCellsPerLight) {
            return failure(WorldLightGridBuildError::CellsPerLightExceeded);
        }

        const float radiusSquared = radius * radius;
        for (int64_t z = minimumCell.z; z <= maximumCell.z; ++z) {
            for (int64_t y = minimumCell.y; y <= maximumCell.y; ++y) {
                for (int64_t x = minimumCell.x; x <= maximumCell.x; ++x) {
                    const glm::ivec3 coordinate{static_cast<int32_t>(x), static_cast<int32_t>(y),
                                                static_cast<int32_t>(z)};
                    if (sphereIntersectsCell(center, radiusSquared, coordinate)) {
                        if (globalIndices.size() + pairs.size() >= kWorldLightGridMaxIndexCount) {
                            return failure(WorldLightGridBuildError::IndexCapacityExceeded);
                        }
                        pairs.push_back({coordinate, lightIndex});
                    }
                }
            }
        }
    }

    std::sort(pairs.begin(), pairs.end(), [](const CellLightPair& lhs, const CellLightPair& rhs) {
        return coordinateLess(lhs.coordinate, rhs.coordinate) ||
               (sameCoordinate(lhs.coordinate, rhs.coordinate) && lhs.lightIndex < rhs.lightIndex);
    });

    WorldLightGridBuildResult result;
    const uint32_t globalLightCount = static_cast<uint32_t>(globalIndices.size());
    result.lightIndices = std::move(globalIndices);
    size_t pairIndex = 0u;
    while (pairIndex < pairs.size()) {
        const glm::ivec3 coordinate = pairs[pairIndex].coordinate;
        const size_t firstPair = pairIndex;
        while (pairIndex < pairs.size() && sameCoordinate(pairs[pairIndex].coordinate, coordinate)) {
            ++pairIndex;
        }
        const size_t cellLightCount = pairIndex - firstPair;
        if (cellLightCount > kWorldLightGridMaxIndexCount ||
            result.lightIndices.size() > kWorldLightGridMaxIndexCount - cellLightCount ||
            result.cells.size() >= kWorldLightGridMaxIndexCount) {
            return failure(WorldLightGridBuildError::IndexCapacityExceeded);
        }
        const uint32_t offset = static_cast<uint32_t>(result.lightIndices.size());
        const uint32_t count = static_cast<uint32_t>(cellLightCount);
        result.cells.push_back(
            {glm::ivec4(coordinate, 0), glm::uvec4(offset, count, 0u, kWorldLightGridContractVersion)});
        result.maxLightsPerCell = std::max(result.maxLightsPerCell, count);
        for (size_t index = firstPair; index < pairIndex; ++index) {
            result.lightIndices.push_back(pairs[index].lightIndex);
        }
    }

    result.header.countsAndVersion = {static_cast<uint32_t>(result.cells.size()),
                                      static_cast<uint32_t>(result.lightIndices.size()), globalLightCount,
                                      kWorldLightGridContractVersion};
    return result;
}

const char* worldLightGridBuildErrorStableId(const WorldLightGridBuildError error) {
    switch (error) {
    case WorldLightGridBuildError::None: return "None";
    case WorldLightGridBuildError::TooManyLights: return "TooManyLights";
    case WorldLightGridBuildError::InvalidLight: return "InvalidLight";
    case WorldLightGridBuildError::CoordinateOverflow: return "CoordinateOverflow";
    case WorldLightGridBuildError::CellsPerLightExceeded: return "CellsPerLightExceeded";
    case WorldLightGridBuildError::IndexCapacityExceeded: return "IndexCapacityExceeded";
    }
    std::abort();
}

} // namespace renderer::contracts
