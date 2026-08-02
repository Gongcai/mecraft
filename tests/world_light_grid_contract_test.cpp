#include "renderer/contracts/ClusteredLightingContract.h"
#include "renderer/contracts/WorldLightGridContract.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

namespace {

[[nodiscard]] bool requireTrue(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[world_light_grid_contract_test] " << message << '\n';
        return false;
    }
    return true;
}

[[nodiscard]] renderer::contracts::GpuLightNormalizationResult makeDirectionalLight(const uint32_t stableId) {
    using namespace renderer::contracts;
    GpuLightNormalizationInput input;
    input.lightId = StableLightId{stableId};
    input.type = GpuLightType::Directional;
    input.emissionDirection = {0.0f, -1.0f, 0.0f};
    input.intensity = 1000.0f;
    input.intensityUnit = GpuLightIntensityUnit::Lux;
    return normalizeGpuLight(input);
}

[[nodiscard]] renderer::contracts::GpuLightNormalizationResult
makeFiniteLight(const renderer::contracts::GpuLightType type, const uint32_t stableId, const glm::vec3 position,
                const float range) {
    using namespace renderer::contracts;
    GpuLightNormalizationInput input;
    input.lightId = StableLightId{stableId};
    input.type = type;
    input.positionMeters = position;
    input.rangeMeters = range;
    input.intensity = 100.0f;
    input.intensityUnit = type == GpuLightType::Rect ? GpuLightIntensityUnit::Nit : GpuLightIntensityUnit::Candela;
    if (type == GpuLightType::Spot) {
        input.emissionDirection = {0.0f, 0.0f, -1.0f};
        input.innerConeAngleRadians = 0.2f;
        input.outerConeAngleRadians = 0.4f;
    } else if (type == GpuLightType::Rect) {
        input.emissionDirection = {0.0f, 0.0f, -1.0f};
        input.rectSizeMeters = {0.5f, 0.5f};
    }
    return normalizeGpuLight(input);
}

[[nodiscard]] bool cellMatches(const renderer::contracts::GpuWorldLightCell& cell, const glm::ivec3 coordinate,
                               const uint32_t offset, const uint32_t count) {
    using namespace renderer::contracts;
    return cell.coordinate == glm::ivec4(coordinate, 0) &&
           cell.indexRangeAndVersion == glm::uvec4(offset, count, 0u, kWorldLightGridContractVersion);
}

[[nodiscard]] bool testEmptyGrid() {
    using namespace renderer::contracts;
    const WorldLightGridBuildResult grid = buildWorldLightGrid({});
    return requireTrue(
        grid.succeeded() && grid.cells.empty() && grid.lightIndices.empty() && grid.maxLightsPerCell == 0u &&
            grid.header.cellSizeAndInverse ==
                glm::vec4(kWorldLightGridCellSizeMeters, 1.0f / kWorldLightGridCellSizeMeters, 0.0f, 0.0f) &&
            grid.header.countsAndVersion == glm::uvec4(0u, 0u, 0u, kWorldLightGridContractVersion),
        "empty input must produce a complete zero-count grid");
}

[[nodiscard]] bool testGlobalPrefixAndFiniteCoverage() {
    using namespace renderer::contracts;
    const GpuLightNormalizationResult directional0 = makeDirectionalLight(11u);
    const GpuLightNormalizationResult point = makeFiniteLight(GpuLightType::Point, 12u, {24.0f, 8.0f, 8.0f}, 1.0f);
    const GpuLightNormalizationResult spot = makeFiniteLight(GpuLightType::Spot, 13u, {-8.0f, 8.0f, 8.0f}, 1.0f);
    const GpuLightNormalizationResult rect = makeFiniteLight(GpuLightType::Rect, 14u, {8.0f, 24.0f, 8.0f}, 1.0f);
    const GpuLightNormalizationResult directional1 = makeDirectionalLight(15u);
    if (!requireTrue(directional0.succeeded() && point.succeeded() && spot.succeeded() && rect.succeeded() &&
                         directional1.succeeded(),
                     "test lights must satisfy the GPU light contract")) {
        return false;
    }

    const WorldLightGridBuildResult grid =
        buildWorldLightGrid({directional0.light, point.light, spot.light, rect.light, directional1.light});
    return requireTrue(
        grid.succeeded() && grid.header.countsAndVersion == glm::uvec4(3u, 5u, 2u, 1u) && grid.cells.size() == 3u &&
            grid.lightIndices == std::vector<uint32_t>({0u, 4u, 2u, 3u, 1u}) &&
            cellMatches(grid.cells[0], {-1, 0, 0}, 2u, 1u) && cellMatches(grid.cells[1], {0, 1, 0}, 3u, 1u) &&
            cellMatches(grid.cells[2], {1, 0, 0}, 4u, 1u) && grid.maxLightsPerCell == 1u,
        "directional lights must form the global prefix and finite light types must occupy sorted cells");
}

[[nodiscard]] bool testStableCellAndLightOrdering() {
    using namespace renderer::contracts;
    const GpuLightNormalizationResult positive = makeFiniteLight(GpuLightType::Point, 21u, {40.0f, 8.0f, 8.0f}, 1.0f);
    const GpuLightNormalizationResult shared0 = makeFiniteLight(GpuLightType::Point, 22u, {8.0f, 8.0f, 8.0f}, 1.0f);
    const GpuLightNormalizationResult shared1 = makeFiniteLight(GpuLightType::Point, 23u, {9.0f, 8.0f, 8.0f}, 1.0f);
    const GpuLightNormalizationResult negative = makeFiniteLight(GpuLightType::Point, 24u, {-24.0f, 8.0f, 8.0f}, 1.0f);
    if (!requireTrue(positive.succeeded() && shared0.succeeded() && shared1.succeeded() && negative.succeeded(),
                     "ordering test lights must normalize")) {
        return false;
    }

    const WorldLightGridBuildResult grid =
        buildWorldLightGrid({positive.light, shared0.light, shared1.light, negative.light});
    return requireTrue(grid.succeeded() && grid.lightIndices == std::vector<uint32_t>({3u, 1u, 2u, 0u}) &&
                           cellMatches(grid.cells[0], {-2, 0, 0}, 0u, 1u) &&
                           cellMatches(grid.cells[1], {0, 0, 0}, 1u, 2u) &&
                           cellMatches(grid.cells[2], {2, 0, 0}, 3u, 1u) && grid.maxLightsPerCell == 2u,
                       "cells and source light indices must use deterministic ascending order");
}

[[nodiscard]] bool testBoundaryIntersection() {
    using namespace renderer::contracts;
    const GpuLightNormalizationResult boundary = makeFiniteLight(GpuLightType::Point, 31u, {8.0f, 8.0f, 8.0f}, 8.0f);
    if (!requireTrue(boundary.succeeded(), "boundary light must normalize")) {
        return false;
    }
    const WorldLightGridBuildResult grid = buildWorldLightGrid({boundary.light});
    return requireTrue(
        grid.succeeded() && grid.cells.size() == 4u && grid.lightIndices == std::vector<uint32_t>({0u, 0u, 0u, 0u}) &&
            cellMatches(grid.cells[0], {0, 0, 0}, 0u, 1u) && cellMatches(grid.cells[1], {0, 0, 1}, 1u, 1u) &&
            cellMatches(grid.cells[2], {0, 1, 0}, 2u, 1u) && cellMatches(grid.cells[3], {1, 0, 0}, 3u, 1u),
        "sphere contact at a cell face must count as an intersection without including edge-only cells");
}

[[nodiscard]] bool testFailures() {
    using namespace renderer::contracts;
    const WorldLightGridBuildResult invalid = buildWorldLightGrid({GpuLight{}});

    GpuLightNormalizationResult overflowLight = makeFiniteLight(GpuLightType::Point, 41u, {8.0f, 8.0f, 8.0f}, 1.0f);
    if (!requireTrue(overflowLight.succeeded(), "coordinate-overflow light must normalize before mutation")) {
        return false;
    }
    overflowLight.light.positionAndRange.x = std::numeric_limits<float>::max();
    const WorldLightGridBuildResult coordinateOverflow = buildWorldLightGrid({overflowLight.light});

    const GpuLightNormalizationResult oversized = makeFiniteLight(GpuLightType::Point, 42u, {8.0f, 8.0f, 8.0f}, 512.0f);
    if (!requireTrue(oversized.succeeded(), "oversized light must remain a valid GPU light")) {
        return false;
    }
    const WorldLightGridBuildResult cellLimit = buildWorldLightGrid({oversized.light});
    const WorldLightGridBuildResult tooMany = buildWorldLightGrid(std::vector<GpuLight>(kClusterMaxLightCount + 1u));

    const GpuLightNormalizationResult indexCapacitySource =
        makeFiniteLight(GpuLightType::Point, 43u, {16.0f, 16.0f, 16.0f}, 1.0f);
    if (!requireTrue(indexCapacitySource.succeeded(), "index-capacity light must normalize")) {
        return false;
    }
    constexpr uint32_t kCellsPerCapacityLight = 8u;
    const uint32_t capacityLightCount = kWorldLightGridMaxIndexCount / kCellsPerCapacityLight + 1u;
    std::vector<GpuLight> capacityLights(capacityLightCount, indexCapacitySource.light);
    for (uint32_t index = 0u; index < capacityLightCount; ++index) {
        capacityLights[index].classificationAndIdentity.y = index + 1u;
    }
    const WorldLightGridBuildResult indexCapacity = buildWorldLightGrid(capacityLights);

    const auto noPartialData = [](const WorldLightGridBuildResult& result) {
        return result.cells.empty() && result.lightIndices.empty() && result.maxLightsPerCell == 0u;
    };
    return requireTrue(invalid.error == WorldLightGridBuildError::InvalidLight && noPartialData(invalid) &&
                           coordinateOverflow.error == WorldLightGridBuildError::CoordinateOverflow &&
                           noPartialData(coordinateOverflow) &&
                           cellLimit.error == WorldLightGridBuildError::CellsPerLightExceeded &&
                           noPartialData(cellLimit) && tooMany.error == WorldLightGridBuildError::TooManyLights &&
                           noPartialData(tooMany) &&
                           indexCapacity.error == WorldLightGridBuildError::IndexCapacityExceeded &&
                           noPartialData(indexCapacity),
                       "invalid records, coordinates, per-light coverage, light counts, and total indices must fail "
                       "atomically") &&
           requireTrue(
               std::string(worldLightGridBuildErrorStableId(WorldLightGridBuildError::None)) == "None" &&
                   std::string(worldLightGridBuildErrorStableId(WorldLightGridBuildError::TooManyLights)) ==
                       "TooManyLights" &&
                   std::string(worldLightGridBuildErrorStableId(WorldLightGridBuildError::InvalidLight)) ==
                       "InvalidLight" &&
                   std::string(worldLightGridBuildErrorStableId(WorldLightGridBuildError::CoordinateOverflow)) ==
                       "CoordinateOverflow" &&
                   std::string(worldLightGridBuildErrorStableId(WorldLightGridBuildError::CellsPerLightExceeded)) ==
                       "CellsPerLightExceeded" &&
                   std::string(worldLightGridBuildErrorStableId(WorldLightGridBuildError::IndexCapacityExceeded)) ==
                       "IndexCapacityExceeded",
               "world-grid error identifiers must remain stable");
}

[[nodiscard]] bool testShaderMirror() {
    const std::string contractPath =
        std::string(MECRAFT_TEST_SOURCE_DIR) + "/assets/shaders/world_light_grid_contract.glsl";
    const std::string queryPath = std::string(MECRAFT_TEST_SOURCE_DIR) + "/assets/shaders/world_light_grid.glsl";
    std::ifstream contractFile(contractPath, std::ios::binary);
    std::ifstream queryFile(queryPath, std::ios::binary);
    if (!contractFile.is_open() || !queryFile.is_open()) {
        return false;
    }
    const std::string contractSource{std::istreambuf_iterator<char>(contractFile), std::istreambuf_iterator<char>()};
    const std::string querySource{std::istreambuf_iterator<char>(queryFile), std::istreambuf_iterator<char>()};
    return requireTrue(contractSource.find("const uint WORLD_LIGHT_GRID_CONTRACT_VERSION = 1u;") != std::string::npos &&
                           contractSource.find("struct WorldLightCell") != std::string::npos &&
                           contractSource.find("ivec4 coordinate;") != std::string::npos &&
                           contractSource.find("uvec4 indexRangeAndVersion;") != std::string::npos &&
                           contractSource.find("struct WorldLightGridHeader") != std::string::npos &&
                           contractSource.find("vec4 cellSizeAndInverse;") != std::string::npos &&
                           contractSource.find("uvec4 countsAndVersion;") != std::string::npos &&
                           querySource.find("binding = 7") != std::string::npos &&
                           querySource.find("binding = 8") != std::string::npos &&
                           querySource.find("binding = 9") != std::string::npos &&
                           querySource.find("worldLightGridCompareCoordinate") != std::string::npos &&
                           querySource.find("bool worldLightGridCellRange") != std::string::npos &&
                           querySource.find("uint middle = low + (high - low) / 2u;") != std::string::npos,
                       "GLSL fields, bindings, version, and binary search must mirror the C++ world-grid contract");
}

} // namespace

int main() {
    bool valid = true;
    valid = testEmptyGrid() && valid;
    valid = testGlobalPrefixAndFiniteCoverage() && valid;
    valid = testStableCellAndLightOrdering() && valid;
    valid = testBoundaryIntersection() && valid;
    valid = testFailures() && valid;
    valid = testShaderMirror() && valid;
    return valid ? EXIT_SUCCESS : EXIT_FAILURE;
}
