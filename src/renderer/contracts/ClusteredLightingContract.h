#ifndef MECRAFT_CLUSTERED_LIGHTING_CONTRACT_H
#define MECRAFT_CLUSTERED_LIGHTING_CONTRACT_H

#include "GpuLightContract.h"

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <optional>
#include <type_traits>
#include <vector>

namespace renderer::contracts {

inline constexpr uint32_t kClusterTileWidth = 16u;
inline constexpr uint32_t kClusterTileHeight = 16u;
inline constexpr uint32_t kClusterDepthSliceCount = 24u;
inline constexpr uint32_t kClusterCoverageWorkgroupSize = 64u;
inline constexpr uint32_t kClusterMaxLightCount = 65535u;
inline constexpr uint32_t kClusterScanWorkgroupSize = 256u;
inline constexpr uint32_t kClusterScanElementsPerWorkgroup =
    kClusterScanWorkgroupSize * 2u;

/// Describes the view-space cluster lattice used by Deferred and Forward+.
struct ClusterGrid final {
    uint32_t renderWidth = 0u;
    uint32_t renderHeight = 0u;
    uint32_t tileCountX = 0u;
    uint32_t tileCountY = 0u;
    uint32_t depthSliceCount = 0u;
    uint32_t clusterCount = 0u;
    float nearPlane = 0.0f;
    float farPlane = 0.0f;
    float depthLogScale = 0.0f;
    float depthLogBias = 0.0f;
};

/// Stores one inclusive integer cluster box consumed by count and fill
/// compute passes. minCluster.w is one for an intersecting light and zero for
/// a light outside the current view. ClusteredLightingPass stores the source
/// GpuLight index in maxCluster.w after removing inactive bounds.
struct alignas(16) GpuClusterLightBounds final {
    glm::uvec4 minCluster{0u};
    glm::uvec4 maxCluster{0u};
};

/// Builds the fixed 16x16x24 cluster lattice for one render extent.
/// @param renderWidth Active render width in pixels.
/// @param renderHeight Active render height in pixels.
/// @param nearPlane Positive perspective near plane in meters.
/// @param farPlane Perspective far plane in meters, greater than nearPlane.
/// @return Complete grid, or std::nullopt for invalid or overflowing inputs.
[[nodiscard]] std::optional<ClusterGrid>
buildClusterGrid(uint32_t renderWidth,
                 uint32_t renderHeight,
                 float nearPlane,
                 float farPlane);

/// Converts a positive view-space depth to its logarithmic Z slice.
/// @param grid Valid cluster grid returned by buildClusterGrid().
/// @param viewDepth Positive distance along the camera forward axis.
/// @return Clamped slice index in [0, depthSliceCount - 1].
[[nodiscard]] uint32_t clusterDepthSlice(const ClusterGrid& grid,
                                         float viewDepth);

/// Computes the conservative inclusive cluster box for one normalized light.
/// Local light bounds use their finite influence sphere; directional lights
/// cover the complete lattice.
/// @param light Fixed GPU light record in camera-relative world coordinates.
/// @param grid Current view cluster lattice.
/// @param view Camera view matrix; only its rotation is applied to positions.
/// @param projection Current perspective projection matrix.
/// @return Bounds with an explicit inactive marker, or std::nullopt when any
/// input violates the fixed contract.
[[nodiscard]] std::optional<GpuClusterLightBounds>
buildGpuClusterLightBounds(const GpuLight& light,
                           const ClusterGrid& grid,
                           const glm::mat4& view,
                           const glm::mat4& projection);

/// Counts the exact number of compact-list entries represented by bounds.
/// @param bounds Inclusive cluster box with its active marker.
/// @return Zero for an inactive light, or the covered cluster count.
[[nodiscard]] uint32_t clusterLightCoverageCount(
    const GpuClusterLightBounds& bounds);

/// Sums all light coverage counts without exceeding 32-bit GPU indices.
/// @param bounds Per-light inclusive cluster boxes.
/// @return Required compact index capacity, or std::nullopt on overflow.
[[nodiscard]] std::optional<uint32_t> requiredClusterLightIndexCount(
    const std::vector<GpuClusterLightBounds>& bounds);

static_assert(sizeof(GpuClusterLightBounds) == 32u);
static_assert(alignof(GpuClusterLightBounds) == 16u);
static_assert(std::is_trivially_copyable_v<GpuClusterLightBounds>);
static_assert(std::is_standard_layout_v<GpuClusterLightBounds>);

} // namespace renderer::contracts

#endif // MECRAFT_CLUSTERED_LIGHTING_CONTRACT_H
