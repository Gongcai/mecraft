#ifndef MECRAFT_TERRAIN_OPACITY_MICROMAP_CONTRACT_H
#define MECRAFT_TERRAIN_OPACITY_MICROMAP_CONTRACT_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace renderer::contracts {

inline constexpr uint16_t kTerrainOpacityMicromapFourStateFormat = 2u;

/// Identifies the immutable level-zero RGBA8 texture-array content used by terrain alpha testing.
struct TerrainOpacityMicromapSource final {
    const uint8_t* rgba8 = nullptr;
    size_t sizeBytes = 0u;
    uint32_t tileSize = 0u;
    uint32_t layerCount = 0u;
    uint64_t alphaTextureHash = 0u;
};

/// Fixes the alpha boundary and micro-triangle density used for terrain opacity micromaps.
struct TerrainOpacityMicromapProfile final {
    uint32_t subdivisionLevel = 4u;
    float alphaCutoff = 0.1f;
};

/// Supplies the material identity and UVs for one non-indexed terrain cutout primitive.
struct TerrainOpacityMicromapTriangleInput final {
    std::array<float, 6u> uv{};
    uint32_t firstTextureLayer = 0u;
    uint32_t animationFrameCount = 1u;
    bool animated = false;
};

/// Matches VkMicromapTriangleEXT without exposing Vulkan headers to terrain CPU preparation.
struct TerrainOpacityMicromapTriangleRecord final {
    uint32_t dataOffset = 0u;
    uint16_t subdivisionLevel = 0u;
    uint16_t format = kTerrainOpacityMicromapFourStateFormat;
};

static_assert(sizeof(TerrainOpacityMicromapTriangleRecord) == 8u,
              "Terrain OMM triangle records must preserve the Vulkan binary layout");

/// Aggregates classified micro-triangle states for diagnostics and A/B reports.
struct TerrainOpacityMicromapCounters final {
    uint64_t opaque = 0u;
    uint64_t transparent = 0u;
    uint64_t unknown = 0u;
};

/// Contains one complete 4-state OMM CPU payload for a terrain cutout geometry.
struct TerrainOpacityMicromapCpuData final {
    uint64_t alphaTextureHash = 0u;
    uint64_t profileHash = 0u;
    uint32_t subdivisionLevel = 0u;
    std::vector<uint8_t> opacityData;
    std::vector<TerrainOpacityMicromapTriangleRecord> triangleRecords;
    TerrainOpacityMicromapCounters counters;
};

/// Validates source dimensions, byte ownership, and stable texture identity.
[[nodiscard]] bool validTerrainOpacityMicromapSource(const TerrainOpacityMicromapSource& source);

/// Validates the fixed alpha policy and a representable 4-state subdivision level.
[[nodiscard]] bool validTerrainOpacityMicromapProfile(const TerrainOpacityMicromapProfile& profile);

/// Returns the stable identity of a valid terrain OMM profile.
[[nodiscard]] uint64_t terrainOpacityMicromapProfileHash(const TerrainOpacityMicromapProfile& profile);

/// Classifies every micro-triangle against all active animation frames and packs 4-state OMM data.
/// @param source Immutable array pixels with the same orientation and layers as the sampled GPU texture.
/// @param profile Fixed alpha boundary and subdivision level.
/// @param triangles Cutout primitives in the exact BLAS primitive order.
/// @return A complete Vulkan-layout payload, or no value when a source/profile/triangle violates the contract.
[[nodiscard]] std::optional<TerrainOpacityMicromapCpuData>
buildTerrainOpacityMicromapCpuData(const TerrainOpacityMicromapSource& source,
                                   const TerrainOpacityMicromapProfile& profile,
                                   const std::vector<TerrainOpacityMicromapTriangleInput>& triangles);

} // namespace renderer::contracts

#endif // MECRAFT_TERRAIN_OPACITY_MICROMAP_CONTRACT_H
