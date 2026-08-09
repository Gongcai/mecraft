#include "renderer/contracts/TerrainOpacityMicromapContract.h"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

[[nodiscard]] bool require(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << "[terrain_opacity_micromap_contract_test] " << message << '\n';
        return false;
    }
    return true;
}

[[nodiscard]] renderer::contracts::TerrainOpacityMicromapTriangleInput makeTriangle(const bool animated = false,
                                                                                    const uint32_t frames = 1u) {
    renderer::contracts::TerrainOpacityMicromapTriangleInput triangle;
    triangle.uv = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    triangle.firstTextureLayer = 0u;
    triangle.animationFrameCount = frames;
    triangle.animated = animated;
    return triangle;
}

[[nodiscard]] renderer::contracts::TerrainOpacityMicromapSource makeSource(const std::vector<uint8_t>& pixels,
                                                                           const uint32_t layers) {
    return {pixels.data(), pixels.size(), 2u, layers, 0x9e3779b97f4a7c15u};
}

} // namespace

int main() {
    using namespace renderer::contracts;
    TerrainOpacityMicromapProfile profile;
    profile.subdivisionLevel = 1u;

    std::vector<uint8_t> opaquePixels(2u * 2u * 4u, 255u);
    const auto opaque = buildTerrainOpacityMicromapCpuData(makeSource(opaquePixels, 1u), profile, {makeTriangle()});

    std::vector<uint8_t> transparentPixels(2u * 2u * 4u, 0u);
    const auto transparent =
        buildTerrainOpacityMicromapCpuData(makeSource(transparentPixels, 1u), profile, {makeTriangle()});

    std::vector<uint8_t> animatedPixels(2u * 2u * 4u * 2u, 255u);
    for (size_t offset = 2u * 2u * 4u; offset < animatedPixels.size(); offset += 4u) {
        animatedPixels[offset + 3u] = 0u;
    }
    const auto animated =
        buildTerrainOpacityMicromapCpuData(makeSource(animatedPixels, 2u), profile, {makeTriangle(true, 2u)});

    TerrainOpacityMicromapSource invalidSource = makeSource(opaquePixels, 1u);
    invalidSource.sizeBytes -= 1u;
    const auto invalid = buildTerrainOpacityMicromapCpuData(invalidSource, profile, {makeTriangle()});

    const bool valid =
        require(validTerrainOpacityMicromapProfile(profile) && terrainOpacityMicromapProfileHash(profile) != 0u,
                "fixed terrain alpha profile must have a stable non-zero identity") &&
        require(opaque.has_value() && opaque->opacityData.size() == 1u && opaque->opacityData[0] == 0x55u &&
                    opaque->triangleRecords.size() == 1u && opaque->triangleRecords[0].dataOffset == 0u &&
                    opaque->triangleRecords[0].subdivisionLevel == 1u &&
                    opaque->triangleRecords[0].format == renderer::contracts::kTerrainOpacityMicromapFourStateFormat &&
                    opaque->counters.opaque == 4u && opaque->counters.transparent == 0u &&
                    opaque->counters.unknown == 0u,
                "opaque texels must produce packed four-state opaque micro-triangles") &&
        require(transparent.has_value() && transparent->opacityData.size() == 1u && transparent->opacityData[0] == 0u &&
                    transparent->counters.opaque == 0u && transparent->counters.transparent == 4u &&
                    transparent->counters.unknown == 0u,
                "transparent texels must produce packed four-state transparent micro-triangles") &&
        require(animated.has_value() && animated->opacityData.size() == 1u && animated->opacityData[0] == 0xffu &&
                    animated->counters.opaque == 0u && animated->counters.transparent == 0u &&
                    animated->counters.unknown == 4u,
                "animation frames with divergent opacity must remain unknown") &&
        require(!invalid.has_value(), "invalid source byte ownership must reject OMM preparation");

    return valid ? EXIT_SUCCESS : EXIT_FAILURE;
}
