#ifndef MECRAFT_WORLD_LIGHT_GRID_CONTRACT_H
#define MECRAFT_WORLD_LIGHT_GRID_CONTRACT_H

#include "GpuLightContract.h"

#include <glm/vec4.hpp>

#include <cstdint>
#include <type_traits>
#include <vector>

namespace renderer::contracts {

inline constexpr uint32_t kWorldLightGridContractVersion = 1u;
inline constexpr float kWorldLightGridCellSizeMeters = 16.0f;
inline constexpr uint32_t kWorldLightGridMaxCellsPerLight = 4096u;
/// Bounds the complete global-prefix and finite-cell index product.
inline constexpr uint32_t kWorldLightGridMaxIndexCount = 262144u;

/// Stores one occupied camera-relative world cell and its compact light-index range.
struct alignas(16) GpuWorldLightCell final {
    glm::ivec4 coordinate{0};
    glm::uvec4 indexRangeAndVersion{0u, 0u, 0u, kWorldLightGridContractVersion};

    [[nodiscard]] bool operator==(const GpuWorldLightCell& other) const {
        return coordinate == other.coordinate && indexRangeAndVersion == other.indexRangeAndVersion;
    }
};

/// Describes the complete sparse world-light product consumed by GPU shading.
struct alignas(16) GpuWorldLightGridHeader final {
    glm::vec4 cellSizeAndInverse{kWorldLightGridCellSizeMeters, 1.0f / kWorldLightGridCellSizeMeters, 0.0f, 0.0f};
    glm::uvec4 countsAndVersion{0u, 0u, 0u, kWorldLightGridContractVersion};

    [[nodiscard]] bool operator==(const GpuWorldLightGridHeader& other) const {
        return cellSizeAndInverse == other.cellSizeAndInverse && countsAndVersion == other.countsAndVersion;
    }
};

/// Identifies deterministic sparse-grid construction failures.
enum class WorldLightGridBuildError : uint8_t {
    None,
    TooManyLights,
    InvalidLight,
    CoordinateOverflow,
    CellsPerLightExceeded,
    IndexCapacityExceeded
};

/// Owns one complete CPU-built sparse world-light grid snapshot.
struct WorldLightGridBuildResult final {
    GpuWorldLightGridHeader header;
    std::vector<GpuWorldLightCell> cells;
    std::vector<uint32_t> lightIndices;
    uint32_t maxLightsPerCell = 0u;
    WorldLightGridBuildError error = WorldLightGridBuildError::None;

    /// Reports whether construction produced a complete grid without partial data.
    /// @return True only when no construction error was recorded.
    [[nodiscard]] bool succeeded() const { return error == WorldLightGridBuildError::None; }
};

/// Builds a deterministic sparse camera-relative world grid from one light revision.
/// Directional lights occupy the leading global-index range; finite lights are assigned
/// to every intersecting 16-meter cell after an exact sphere/AABB test.
/// @param lights Complete normalized camera-relative light snapshot.
/// @return Grid records and compact indices, or a precise error without partial output.
[[nodiscard]] WorldLightGridBuildResult buildWorldLightGrid(const std::vector<GpuLight>& lights);

/// Returns a stable identifier for one sparse-grid construction error.
/// @param error Error value to identify.
/// @return Process-lifetime string containing the stable identifier.
[[nodiscard]] const char* worldLightGridBuildErrorStableId(WorldLightGridBuildError error);

static_assert(sizeof(GpuWorldLightCell) == 32u);
static_assert(alignof(GpuWorldLightCell) == 16u);
static_assert(std::is_trivially_copyable_v<GpuWorldLightCell>);
static_assert(std::is_standard_layout_v<GpuWorldLightCell>);
static_assert(sizeof(GpuWorldLightGridHeader) == 32u);
static_assert(alignof(GpuWorldLightGridHeader) == 16u);
static_assert(std::is_trivially_copyable_v<GpuWorldLightGridHeader>);
static_assert(std::is_standard_layout_v<GpuWorldLightGridHeader>);

} // namespace renderer::contracts

#endif // MECRAFT_WORLD_LIGHT_GRID_CONTRACT_H
