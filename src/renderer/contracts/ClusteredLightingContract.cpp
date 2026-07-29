#include "ClusteredLightingContract.h"

#include <glm/geometric.hpp>
#include <glm/mat3x3.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace renderer::contracts {
namespace {

[[nodiscard]] bool finite(const glm::mat4& matrix) {
    for (uint32_t column = 0u; column < 4u; ++column) {
        for (uint32_t row = 0u; row < 4u; ++row) {
            if (!std::isfinite(matrix[column][row])) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] uint32_t tileIndex(const float ndc,
                                 const uint32_t renderExtent,
                                 const uint32_t tileExtent,
                                 const uint32_t tileCount) {
    const float uv = std::clamp(ndc * 0.5f + 0.5f, 0.0f, 1.0f);
    const uint32_t pixel = static_cast<uint32_t>(
        std::min(static_cast<double>(renderExtent - 1u),
                 std::floor(static_cast<double>(uv) * renderExtent)));
    return std::min(pixel / tileExtent, tileCount - 1u);
}

[[nodiscard]] bool validLightRecord(const GpuLight& light) {
    const uint32_t type = light.classificationAndIdentity.x;
    return type <= static_cast<uint32_t>(GpuLightType::Rect) &&
           light.classificationAndIdentity.y != 0u &&
           light.resourcesAndFlags.w == kGpuLightContractVersion &&
           (light.resourcesAndFlags.z & ~kGpuLightKnownContributionFlags) == 0u &&
           gpuLightPackedRangeValid(light);
}

} // namespace

std::optional<ClusterGrid> buildClusterGrid(const uint32_t renderWidth,
                                            const uint32_t renderHeight,
                                            const float nearPlane,
                                            const float farPlane) {
    if (renderWidth == 0u || renderHeight == 0u ||
        !std::isfinite(nearPlane) || !std::isfinite(farPlane) ||
        nearPlane <= 0.0f || farPlane <= nearPlane) {
        return std::nullopt;
    }
    const uint64_t tileCountX =
        (static_cast<uint64_t>(renderWidth) + kClusterTileWidth - 1u) /
        kClusterTileWidth;
    const uint64_t tileCountY =
        (static_cast<uint64_t>(renderHeight) + kClusterTileHeight - 1u) /
        kClusterTileHeight;
    const uint64_t clusterCount =
        tileCountX * tileCountY * kClusterDepthSliceCount;
    if (tileCountX > std::numeric_limits<uint32_t>::max() ||
        tileCountY > std::numeric_limits<uint32_t>::max() ||
        clusterCount > std::numeric_limits<uint32_t>::max()) {
        return std::nullopt;
    }

    const float logarithmicRange = std::log(farPlane / nearPlane);
    if (!std::isfinite(logarithmicRange) || logarithmicRange <= 0.0f) {
        return std::nullopt;
    }
    ClusterGrid grid;
    grid.renderWidth = renderWidth;
    grid.renderHeight = renderHeight;
    grid.tileCountX = static_cast<uint32_t>(tileCountX);
    grid.tileCountY = static_cast<uint32_t>(tileCountY);
    grid.depthSliceCount = kClusterDepthSliceCount;
    grid.clusterCount = static_cast<uint32_t>(clusterCount);
    grid.nearPlane = nearPlane;
    grid.farPlane = farPlane;
    grid.depthLogScale =
        static_cast<float>(kClusterDepthSliceCount) / logarithmicRange;
    grid.depthLogBias = -std::log(nearPlane) * grid.depthLogScale;
    return grid;
}

uint32_t clusterDepthSlice(const ClusterGrid& grid, const float viewDepth) {
    const float clampedDepth =
        std::clamp(viewDepth, grid.nearPlane, grid.farPlane);
    const float slice =
        std::floor(std::log(clampedDepth) * grid.depthLogScale +
                   grid.depthLogBias);
    return std::min(static_cast<uint32_t>(std::max(slice, 0.0f)),
                    grid.depthSliceCount - 1u);
}

std::optional<GpuClusterLightBounds> buildGpuClusterLightBounds(
    const GpuLight& light,
    const ClusterGrid& grid,
    const glm::mat4& view,
    const glm::mat4& projection) {
    if (!validLightRecord(light) || grid.renderWidth == 0u ||
        grid.renderHeight == 0u || grid.tileCountX == 0u ||
        grid.tileCountY == 0u || grid.depthSliceCount == 0u ||
        grid.clusterCount == 0u || !finite(view) || !finite(projection)) {
        return std::nullopt;
    }

    GpuClusterLightBounds bounds;
    const GpuLightType type =
        static_cast<GpuLightType>(light.classificationAndIdentity.x);
    if (type == GpuLightType::Directional) {
        bounds.minCluster = {0u, 0u, 0u, 1u};
        bounds.maxCluster = {grid.tileCountX - 1u,
                             grid.tileCountY - 1u,
                             grid.depthSliceCount - 1u, 0u};
        return bounds;
    }

    const float radius = light.positionAndRange.w;
    if (!std::isfinite(radius) || radius <= 0.0f) {
        return std::nullopt;
    }
    const glm::vec3 cameraRelativePosition(light.positionAndRange);
    if (!std::isfinite(cameraRelativePosition.x) ||
        !std::isfinite(cameraRelativePosition.y) ||
        !std::isfinite(cameraRelativePosition.z)) {
        return std::nullopt;
    }
    const glm::vec3 viewCenter = glm::mat3(view) * cameraRelativePosition;
    const float viewDepth = -viewCenter.z;
    if (viewDepth + radius < grid.nearPlane ||
        viewDepth - radius > grid.farPlane) {
        return bounds;
    }

    const float minDepth = std::max(grid.nearPlane, viewDepth - radius);
    const float maxDepth = std::min(grid.farPlane, viewDepth + radius);
    bounds.minCluster.z = clusterDepthSlice(grid, minDepth);
    bounds.maxCluster.z = clusterDepthSlice(grid, maxDepth);

    bool fullScreen = viewDepth - radius <= grid.nearPlane;
    float ndcMinX = -1.0f;
    float ndcMaxX = 1.0f;
    float ndcMinY = -1.0f;
    float ndcMaxY = 1.0f;
    if (!fullScreen) {
        const glm::vec4 clipCenter =
            projection * glm::vec4(viewCenter, 1.0f);
        const glm::vec3 rowX{projection[0][0], projection[1][0],
                             projection[2][0]};
        const glm::vec3 rowY{projection[0][1], projection[1][1],
                             projection[2][1]};
        const glm::vec3 rowW{projection[0][3], projection[1][3],
                             projection[2][3]};
        const float denominatorRadius = glm::length(rowW) * radius;
        const float minimumW = clipCenter.w - denominatorRadius;
        if (!std::isfinite(clipCenter.w) || minimumW <= 1.0e-5f) {
            fullScreen = true;
        } else {
            const glm::vec2 ndcCenter =
                glm::vec2(clipCenter) / clipCenter.w;
            const float radiusX =
                (glm::length(rowX) * radius +
                 std::abs(ndcCenter.x) * denominatorRadius) /
                minimumW;
            const float radiusY =
                (glm::length(rowY) * radius +
                 std::abs(ndcCenter.y) * denominatorRadius) /
                minimumW;
            ndcMinX = ndcCenter.x - radiusX;
            ndcMaxX = ndcCenter.x + radiusX;
            ndcMinY = ndcCenter.y - radiusY;
            ndcMaxY = ndcCenter.y + radiusY;
            if (ndcMaxX < -1.0f || ndcMinX > 1.0f ||
                ndcMaxY < -1.0f || ndcMinY > 1.0f) {
                return bounds;
            }
        }
    }
    if (fullScreen) {
        bounds.minCluster.x = 0u;
        bounds.minCluster.y = 0u;
        bounds.maxCluster.x = grid.tileCountX - 1u;
        bounds.maxCluster.y = grid.tileCountY - 1u;
    } else {
        bounds.minCluster.x = tileIndex(
            ndcMinX, grid.renderWidth, kClusterTileWidth, grid.tileCountX);
        bounds.minCluster.y = tileIndex(
            ndcMinY, grid.renderHeight, kClusterTileHeight, grid.tileCountY);
        bounds.maxCluster.x = tileIndex(
            ndcMaxX, grid.renderWidth, kClusterTileWidth, grid.tileCountX);
        bounds.maxCluster.y = tileIndex(
            ndcMaxY, grid.renderHeight, kClusterTileHeight, grid.tileCountY);
    }
    bounds.minCluster.w = 1u;
    return bounds;
}

uint32_t clusterLightCoverageCount(const GpuClusterLightBounds& bounds) {
    if (bounds.minCluster.w == 0u) {
        return 0u;
    }
    const uint64_t countX =
        static_cast<uint64_t>(bounds.maxCluster.x) - bounds.minCluster.x + 1u;
    const uint64_t countY =
        static_cast<uint64_t>(bounds.maxCluster.y) - bounds.minCluster.y + 1u;
    const uint64_t countZ =
        static_cast<uint64_t>(bounds.maxCluster.z) - bounds.minCluster.z + 1u;
    const uint64_t count = countX * countY * countZ;
    return count <= std::numeric_limits<uint32_t>::max()
        ? static_cast<uint32_t>(count)
        : 0u;
}

std::optional<uint32_t> requiredClusterLightIndexCount(
    const std::vector<GpuClusterLightBounds>& bounds) {
    uint64_t total = 0u;
    for (const GpuClusterLightBounds& lightBounds : bounds) {
        const uint32_t count = clusterLightCoverageCount(lightBounds);
        if (lightBounds.minCluster.w != 0u && count == 0u) {
            return std::nullopt;
        }
        total += count;
        if (total > std::numeric_limits<uint32_t>::max()) {
            return std::nullopt;
        }
    }
    return static_cast<uint32_t>(total);
}

} // namespace renderer::contracts
