#include "renderer/contracts/ReflectionProbeContract.h"

#include <glm/geometric.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

namespace {

bool requireTrue(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[reflection_probe_contract_test] FAIL: "
                  << message << '\n';
        return false;
    }
    return true;
}

bool readProjectFile(const char* relativePath, std::string& source) {
    const std::string path = std::string(MECRAFT_TEST_SOURCE_DIR) +
        "/" + relativePath;
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open()) {
        return false;
    }
    source.assign(std::istreambuf_iterator<char>(stream),
                  std::istreambuf_iterator<char>());
    return true;
}

bool nearlyEqual(const float lhs, const float rhs,
                 const float tolerance = 1.0e-5f) {
    return std::abs(lhs - rhs) <= tolerance;
}

bool nearlyEqual(const glm::vec3& lhs, const glm::vec3& rhs,
                 const float tolerance = 1.0e-5f) {
    return nearlyEqual(lhs.x, rhs.x, tolerance) &&
           nearlyEqual(lhs.y, rhs.y, tolerance) &&
           nearlyEqual(lhs.z, rhs.z, tolerance);
}

renderer::contracts::ReflectionProbeNormalizationInput makeInput(
    const uint32_t stableId,
    const glm::vec3 position = {0.0f, 1.0f, 0.0f},
    const float validity = 1.0f) {
    using namespace renderer::contracts;
    ReflectionProbeNormalizationInput input;
    input.probeId = StableReflectionProbeId{stableId};
    input.positionMeters = position;
    input.exposureScale = 1.25f;
    input.influenceMinMeters = {-4.0f, 0.0f, -4.0f};
    input.influenceMaxMeters = {4.0f, 4.0f, 4.0f};
    input.blendDistanceMeters = 1.0f;
    input.boxProjectionMinMeters = {-5.0f, -1.0f, -5.0f};
    input.boxProjectionMaxMeters = {5.0f, 5.0f, 5.0f};
    input.validity = validity;
    input.prefilteredCubemapIndex = validity > 0.0f
        ? stableId + 100u
        : kReflectionProbeInvalidCubemapIndex;
    input.captureRevision = validity > 0.0f ? 7u : 0u;
    return input;
}

renderer::contracts::GpuReflectionProbe makeProbe(
    const uint32_t stableId,
    const glm::vec3 position = {0.0f, 1.0f, 0.0f},
    const float validity = 1.0f) {
    const auto result = renderer::contracts::normalizeReflectionProbe(
        makeInput(stableId, position, validity));
    return result.probe;
}

bool testNormalizationAndPackedLayout() {
    using namespace renderer::contracts;
    const ReflectionProbeNormalizationResult result =
        normalizeReflectionProbe(makeInput(42u));
    if (!requireTrue(result.succeeded(),
                     "valid probe values must normalize") ||
        !requireTrue(validateReflectionProbe(result.probe).succeeded(),
                     "normalized probe records must validate")) {
        return false;
    }

    const GpuReflectionProbe& probe = result.probe;
    return requireTrue(
               glm::vec3(probe.positionAndExposure) ==
                       glm::vec3(0.0f, 1.0f, 0.0f) &&
                   probe.positionAndExposure.w == 1.25f,
               "position and exposure must occupy the first vector") &&
           requireTrue(
               probe.influenceMinAndBlendDistance.w == 1.0f &&
                   probe.influenceMaxAndValidity.w == 1.0f,
               "blend distance and validity must occupy the AABB vectors") &&
           requireTrue(
               probe.resourcesAndIdentity ==
                   glm::uvec4(142u, 42u, 7u,
                              kReflectionProbeContractVersion),
               "resource, stable identity, revision, and version must be packed") &&
           requireTrue(
               probe.boxProjectionMin.w == 0.0f &&
                   probe.boxProjectionMax.w == 0.0f,
               "reserved projection components must remain zero");
}

bool testStructuredValidationFailures() {
    using namespace renderer::contracts;
    ReflectionProbeNormalizationInput input = makeInput(1u);
    input.probeId = {};
    const auto invalidId = normalizeReflectionProbe(input);
    if (!requireTrue(
            !invalidId.succeeded() &&
                invalidId.error == ReflectionProbeError::InvalidStableId &&
                invalidId.field == ReflectionProbeField::StableId,
            "zero stable IDs must fail with their exact semantic field")) {
        return false;
    }

    input = makeInput(2u);
    input.blendDistanceMeters = 3.0f;
    const auto invalidBlend = normalizeReflectionProbe(input);
    if (!requireTrue(
            !invalidBlend.succeeded() &&
                invalidBlend.error ==
                    ReflectionProbeError::InvalidBlendDistance,
            "blend distances larger than half the narrowest extent must fail")) {
        return false;
    }

    input = makeInput(3u);
    input.boxProjectionMinMeters = {-3.0f, -1.0f, -5.0f};
    const auto outsideProjection = normalizeReflectionProbe(input);
    if (!requireTrue(
            !outsideProjection.succeeded() &&
                outsideProjection.error ==
                    ReflectionProbeError::InfluenceOutsideBoxProjectionBounds,
            "projection bounds must contain the complete influence volume")) {
        return false;
    }

    input = makeInput(4u);
    input.validity = 0.5f;
    input.prefilteredCubemapIndex =
        kReflectionProbeInvalidCubemapIndex;
    const auto captureConflict = normalizeReflectionProbe(input);
    if (!requireTrue(
            !captureConflict.succeeded() &&
                captureConflict.error ==
                    ReflectionProbeError::CaptureStateConflict &&
                captureConflict.field ==
                    ReflectionProbeField::PrefilteredCubemapIndex,
            "positive validity must require a concrete cubemap resource")) {
        return false;
    }

    const auto uncaptured = normalizeReflectionProbe(
        makeInput(5u, {0.0f, 1.0f, 0.0f}, 0.0f));
    if (!requireTrue(uncaptured.succeeded(),
                     "zero validity with no capture resource must be valid")) {
        return false;
    }

    GpuReflectionProbe packed = makeProbe(6u);
    packed.resourcesAndIdentity.w = 99u;
    const auto invalidVersion = validateReflectionProbe(packed);
    packed = makeProbe(7u);
    packed.boxProjectionMin.w = 1.0f;
    const auto invalidReserved = validateReflectionProbe(packed);
    return requireTrue(
               invalidVersion.error ==
                       ReflectionProbeError::InvalidContractVersion &&
                   invalidVersion.field ==
                       ReflectionProbeField::ContractVersion,
               "packed records must reject unknown contract versions") &&
           requireTrue(
               invalidReserved.error ==
                       ReflectionProbeError::NonZeroReservedValue &&
                   invalidReserved.field ==
                       ReflectionProbeField::ReservedValue,
               "packed records must reject non-zero reserved components") &&
           requireTrue(
               std::string(reflectionProbeErrorStableId(
                   ReflectionProbeError::CaptureStateConflict)) ==
                       "CaptureStateConflict" &&
                   std::string(reflectionProbeFieldStableId(
                       ReflectionProbeField::CaptureRevision)) ==
                       "CaptureRevision",
               "errors and fields must expose stable diagnostic IDs");
}

bool testInfluenceWeights() {
    using namespace renderer::contracts;
    GpuReflectionProbe probe = makeProbe(10u);
    probe.influenceMaxAndValidity.w = 0.8f;

    const auto center = reflectionProbeInfluenceWeight(
        probe, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
    const auto halfway = reflectionProbeInfluenceWeight(
        probe, {0.0f, 1.0f, -2.0f}, {0.0f, 0.0f, 1.0f});
    const auto opposite = reflectionProbeInfluenceWeight(
        probe, {0.0f, 1.0f, -2.0f}, {0.0f, 0.0f, -1.0f});
    const auto boundary = reflectionProbeInfluenceWeight(
        probe, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
    const auto outside = reflectionProbeInfluenceWeight(
        probe, {8.0f, 1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f});
    const auto invalidNormal = reflectionProbeInfluenceWeight(
        probe, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f});

    return requireTrue(center.has_value() && nearlyEqual(*center, 0.8f),
                       "probe-center weight must equal capture validity") &&
           requireTrue(halfway.has_value() && nearlyEqual(*halfway, 0.4f),
                       "normalized probe distance must attenuate influence") &&
           requireTrue(opposite.has_value() && *opposite == 0.0f,
                       "surfaces facing away from a probe must receive zero weight") &&
           requireTrue(boundary.has_value() && *boundary == 0.0f,
                       "influence-box boundaries must reach zero weight") &&
           requireTrue(outside.has_value() && *outside == 0.0f,
                       "surfaces outside an influence box must receive zero weight") &&
           requireTrue(!invalidNormal.has_value(),
                       "zero surface normals must fail explicitly");
}

bool testDeterministicTopFourSelection() {
    using namespace renderer::contracts;
    const std::vector<GpuReflectionProbe> probes{
        makeProbe(50u), makeProbe(20u), makeProbe(40u),
        makeProbe(10u), makeProbe(30u)};
    const ReflectionProbeSelectionResult result = selectReflectionProbes(
        probes, {0.0f, 1.0f, -1.0f}, {0.0f, 0.0f, 1.0f});
    if (!requireTrue(result.succeeded() && result.selection.count == 4u,
                     "selection must return at most four positive probes")) {
        return false;
    }

    constexpr std::array<uint32_t, 4> expectedIds{10u, 20u, 30u, 40u};
    float totalWeight = 0.0f;
    for (uint32_t index = 0u; index < result.selection.count; ++index) {
        if (!requireTrue(
                result.selection.entries[index].probeId.value ==
                    expectedIds[index],
                "equal weights must sort by stable probe ID")) {
            return false;
        }
        totalWeight += result.selection.entries[index].weight;
    }
    if (!requireTrue(nearlyEqual(totalWeight, 1.0f),
                     "selected probe weights must normalize to one")) {
        return false;
    }

    std::vector<GpuReflectionProbe> duplicate{
        makeProbe(1u), makeProbe(1u)};
    const ReflectionProbeSelectionResult duplicateResult =
        selectReflectionProbes(
            duplicate, {0.0f, 1.0f, -1.0f}, {0.0f, 0.0f, 1.0f});
    const ReflectionProbeSelectionResult invalidQuery =
        selectReflectionProbes(
            probes, {0.0f, 1.0f, -1.0f}, {0.0f, 0.0f, 0.0f});
    return requireTrue(
               !duplicateResult.succeeded() &&
                   duplicateResult.error ==
                       ReflectionProbeError::DuplicateStableId &&
                   duplicateResult.probeIndex == 1u &&
                   duplicateResult.probeId.value == 1u,
               "duplicate IDs must identify the exact failing probe") &&
           requireTrue(
               !invalidQuery.succeeded() &&
                   invalidQuery.error ==
                       ReflectionProbeError::InvalidSurfaceNormal,
               "selection must reject an invalid surface query");
}

bool testDeterministicSpatialGrid() {
    using namespace renderer::contracts;
    const std::vector<GpuReflectionProbe> probes{
        makeProbe(50u), makeProbe(20u), makeProbe(40u),
        makeProbe(10u), makeProbe(30u),
        makeProbe(60u, {0.0f, 1.0f, 0.0f}, 0.0f)};
    const ReflectionProbeGridBuildResult result =
        buildReflectionProbeGrid(probes);
    if (!requireTrue(result.succeeded(),
                     "valid probes must build a spatial grid") ||
        !requireTrue(
            result.grid.metadata.dimensionsAndProbeCount ==
                glm::uvec4(2u, 1u, 2u, 5u),
            "grid dimensions and active probe count must be packed") ||
        !requireTrue(
            result.grid.metadata.cellAndIndexCounts ==
                glm::uvec4(4u, 20u,
                           kReflectionProbeGridMaxProbesPerCell,
                           kReflectionProbeContractVersion),
            "cell and compact-index counts must be exact") ||
        !requireTrue(result.grid.probes.size() == 5u &&
                         result.grid.cells.size() == 4u &&
                         result.grid.probeIndices.size() == 20u,
                     "zero-validity probes must be excluded from GPU data")) {
        return false;
    }

    constexpr std::array<uint32_t, 5> expectedIds{
        10u, 20u, 30u, 40u, 50u};
    for (uint32_t index = 0u; index < expectedIds.size(); ++index) {
        if (!requireTrue(
                result.grid.probes[index].resourcesAndIdentity.y ==
                    expectedIds[index],
                "packed probes must sort by stable ID")) {
            return false;
        }
    }
    for (const GpuReflectionProbeGridCell& cell : result.grid.cells) {
        if (!requireTrue(cell.offsetAndCount.y == 5u,
                         "every overlapping cell must reference all probes")) {
            return false;
        }
        for (uint32_t offset = 0u; offset < cell.offsetAndCount.y; ++offset) {
            if (!requireTrue(
                    result.grid.probeIndices[
                        cell.offsetAndCount.x + offset] == offset,
                    "cell candidates must preserve stable-ID order")) {
                return false;
            }
        }
    }
    const auto firstCell = reflectionProbeGridCellIndex(
        result.grid.metadata, {0u, 0u, 0u});
    const auto lastCell = reflectionProbeGridCellIndex(
        result.grid.metadata, {1u, 0u, 1u});
    const auto outsideCell = reflectionProbeGridCellIndex(
        result.grid.metadata, {2u, 0u, 0u});
    return requireTrue(firstCell.has_value() && *firstCell == 0u,
                       "the first grid cell must map to index zero") &&
           requireTrue(lastCell.has_value() && *lastCell == 3u,
                       "xyz coordinates must use the shared linear order") &&
           requireTrue(!outsideCell.has_value(),
                       "out-of-range cell coordinates must fail explicitly");
}

bool testSpatialGridFailures() {
    using namespace renderer::contracts;
    std::vector<GpuReflectionProbe> crowded;
    crowded.reserve(kReflectionProbeGridMaxProbesPerCell + 1u);
    for (uint32_t index = 0u;
         index <= kReflectionProbeGridMaxProbesPerCell; ++index) {
        crowded.push_back(makeProbe(index + 1u));
    }
    const ReflectionProbeGridBuildResult crowdedResult =
        buildReflectionProbeGrid(crowded);
    if (!requireTrue(
            !crowdedResult.succeeded() &&
                crowdedResult.error ==
                    ReflectionProbeGridError::CellCapacityExceeded &&
                crowdedResult.probeId.value == 17u,
            "per-cell overflow must identify the exact probe")) {
        return false;
    }

    ReflectionProbeNormalizationInput largeInput = makeInput(100u);
    largeInput.positionMeters = {1.0f, 1.0f, 0.0f};
    largeInput.influenceMinMeters = {0.0f, 0.0f, -4.0f};
    largeInput.influenceMaxMeters = {2050.0f, 4.0f, 4.0f};
    largeInput.boxProjectionMinMeters = {-1.0f, -1.0f, -5.0f};
    largeInput.boxProjectionMaxMeters = {2051.0f, 5.0f, 5.0f};
    const auto largeProbe = normalizeReflectionProbe(largeInput);
    if (!requireTrue(largeProbe.succeeded(),
                     "large-grid test probe must satisfy probe validation")) {
        return false;
    }
    const ReflectionProbeGridBuildResult largeResult =
        buildReflectionProbeGrid({largeProbe.probe});
    GpuReflectionProbe invalid = makeProbe(200u);
    invalid.resourcesAndIdentity.w = 0u;
    const ReflectionProbeGridBuildResult invalidResult =
        buildReflectionProbeGrid({invalid});
    return requireTrue(
               !largeResult.succeeded() &&
                   largeResult.error ==
                       ReflectionProbeGridError::DimensionExceeded,
               "grid dimensions above the fixed contract must fail") &&
           requireTrue(
               !invalidResult.succeeded() &&
                   invalidResult.error ==
                       ReflectionProbeGridError::InvalidProbe &&
                   invalidResult.probeError ==
                       ReflectionProbeError::InvalidContractVersion &&
                   invalidResult.sourceProbeIndex == 0u,
               "grid failures must preserve packed-probe validation details") &&
           requireTrue(
               std::string(reflectionProbeGridErrorStableId(
                   ReflectionProbeGridError::CellCapacityExceeded)) ==
                   "CellCapacityExceeded",
               "grid failures must expose stable diagnostic IDs");
}

bool testBoxProjection() {
    using namespace renderer::contracts;
    const GpuReflectionProbe probe = makeProbe(30u);
    const auto projected = boxProjectReflectionDirection(
        probe, {1.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 1.0f});
    const glm::vec3 expected = glm::normalize(glm::vec3(5.0f, 0.0f, 4.0f));
    const auto outside = boxProjectReflectionDirection(
        probe, {8.0f, 1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f});
    const auto zeroDirection = boxProjectReflectionDirection(
        probe, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f});
    return requireTrue(
               projected.has_value() && nearlyEqual(*projected, expected),
               "box projection must point from the capture position to the ray exit") &&
           requireTrue(!outside.has_value(),
                       "box projection must reject origins outside its AABB") &&
           requireTrue(!zeroDirection.has_value(),
                       "box projection must reject zero directions");
}

bool testDeterministicCaptureWorkSchedule() {
    using namespace renderer::contracts;
    static_assert(kReflectionProbeCubeFaceCount == 6u);
    static_assert(kReflectionProbeCubeMipCount == 8u);
    static_assert(kReflectionProbeCaptureMaxProbeCount == 128u);
    static_assert(kReflectionProbePrefilterWorkItemCount == 48u);
    static_assert(kReflectionProbeCaptureWorkItemCount == 54u);
    return requireTrue(
               reflectionProbeCaptureWorkKind(0u) ==
                       ReflectionProbeCaptureWorkKind::RadianceFace &&
                   reflectionProbeCaptureFace(0u) == 0u &&
                   reflectionProbeCaptureFace(5u) == 5u,
               "the first six capture items must record ordered radiance faces") &&
           requireTrue(
               reflectionProbeCaptureWorkKind(6u) ==
                       ReflectionProbeCaptureWorkKind::PrefilterFaceMip &&
                   reflectionProbeCaptureFace(6u) == 0u &&
                   reflectionProbeCaptureMip(6u) == 0u &&
                   reflectionProbeCaptureFace(12u) == 0u &&
                   reflectionProbeCaptureMip(12u) == 1u &&
                   reflectionProbeCaptureFace(53u) == 5u &&
                   reflectionProbeCaptureMip(53u) == 7u,
               "prefilter work must advance face-major within every mip") &&
           requireTrue(
               nearlyEqual(reflectionProbeRoughnessForMip(0u), 0.0f) &&
                   nearlyEqual(reflectionProbeRoughnessForMip(7u), 1.0f),
               "prefilter mip endpoints must cover the complete roughness range");
}

bool testCpuAndGlslMirror() {
    using namespace renderer::contracts;
    static_assert(offsetof(GpuReflectionProbe, positionAndExposure) == 0u);
    static_assert(offsetof(GpuReflectionProbe,
                           influenceMinAndBlendDistance) == 16u);
    static_assert(offsetof(GpuReflectionProbe,
                           influenceMaxAndValidity) == 32u);
    static_assert(offsetof(GpuReflectionProbe, boxProjectionMin) == 48u);
    static_assert(offsetof(GpuReflectionProbe, boxProjectionMax) == 64u);
    static_assert(offsetof(GpuReflectionProbe,
                           resourcesAndIdentity) == 80u);
    static_assert(offsetof(GpuReflectionProbeGridMetadata,
                           originAndCellSize) == 0u);
    static_assert(offsetof(GpuReflectionProbeGridMetadata,
                           dimensionsAndProbeCount) == 16u);
    static_assert(offsetof(GpuReflectionProbeGridMetadata,
                           cellAndIndexCounts) == 32u);
    static_assert(offsetof(GpuReflectionProbeGridMetadata,
                           reserved) == 48u);
    static_assert(offsetof(GpuReflectionProbeGridCell,
                           offsetAndCount) == 0u);

    const std::string path = std::string(MECRAFT_TEST_SOURCE_DIR) +
        "/assets/shaders/reflection_probe_contract.glsl";
    std::ifstream stream(path, std::ios::binary);
    if (!requireTrue(stream.is_open(),
                     "reflection-probe GLSL contract must be readable")) {
        return false;
    }
    const std::string source((std::istreambuf_iterator<char>(stream)),
                             std::istreambuf_iterator<char>());
    if (!requireTrue(
            source.find("REFLECTION_PROBE_CONTRACT_VERSION = 1u") !=
                    std::string::npos &&
                source.find("REFLECTION_PROBE_BLEND_COUNT = 4u") !=
                    std::string::npos &&
                source.find("REFLECTION_PROBE_GRID_CELL_SIZE_METERS = 16.0") !=
                    std::string::npos &&
                source.find("REFLECTION_PROBE_GRID_MAX_PROBES_PER_CELL = 16u") !=
                    std::string::npos &&
                source.find("REFLECTION_PROBE_INVALID_CUBEMAP_INDEX = 0xffffffffu") !=
                    std::string::npos,
            "GLSL constants must mirror CPU contract values")) {
        return false;
    }

    constexpr std::array<const char*, 6> fields{
        "vec4 positionAndExposure;",
        "vec4 influenceMinAndBlendDistance;",
        "vec4 influenceMaxAndValidity;",
        "vec4 boxProjectionMin;",
        "vec4 boxProjectionMax;",
        "uvec4 resourcesAndIdentity;"};
    std::size_t offset = 0u;
    for (const char* field : fields) {
        const std::size_t found = source.find(field, offset);
        if (!requireTrue(found != std::string::npos,
                         "GLSL probe fields must mirror CPU order")) {
            return false;
        }
        offset = found + std::string(field).size();
    }
    return requireTrue(
        source.find("reflectionProbeInfluenceWeight") != std::string::npos &&
            source.find("reflectionProbeBoxProject") != std::string::npos &&
            source.find("reflectionProbeGridCellIndex") != std::string::npos,
        "GLSL must expose influence, box-projection, and grid functions");
}

bool testRuntimeGridIntegrationContract() {
    std::string capturePass;
    std::string gridPass;
    std::string reflectionPass;
    std::string reflectionShader;
    std::string pipeline;
    if (!requireTrue(readProjectFile(
                         "src/renderer/passes/ReflectionProbeCapturePass.cpp",
                         capturePass),
                     "probe-capture pass source must be readable") ||
        !requireTrue(readProjectFile(
                         "src/renderer/passes/ReflectionProbeGridPass.cpp",
                         gridPass),
                     "probe-grid pass source must be readable") ||
        !requireTrue(readProjectFile(
                         "src/renderer/passes/ReflectionPass.cpp",
                         reflectionPass),
                     "reflection pass source must be readable") ||
        !requireTrue(readProjectFile(
                         "assets/shaders/reflection_probe.frag",
                         reflectionShader),
                     "reflection shader source must be readable") ||
        !requireTrue(readProjectFile(
                         "src/renderer/core/DeferredPipeline.cpp",
                         pipeline),
                     "deferred pipeline source must be readable")) {
        return false;
    }
    return requireTrue(
               capturePass.find(
                   "kReflectionProbeCaptureWorkItemCount") !=
                       std::string::npos &&
                   capturePass.find(
                       "ReflectionProbeCapture.RadianceFace") !=
                       std::string::npos &&
                   capturePass.find(
                       "ReflectionProbeCapture.PrefilterFaceMip") !=
                       std::string::npos &&
                   capturePass.find(
                       "state.activeSlot = state.buildSlot") !=
                       std::string::npos,
               "capture pass must queue radiance/prefilter work and commit complete slots") &&
           requireTrue(
               gridPass.find("buildReflectionProbeGrid(m_sceneProbes)") !=
                       std::string::npos &&
                   gridPass.find("RhiTextureDimension::CubeArray") !=
                       std::string::npos &&
                   gridPass.find("ReflectionProbeGrid.Upload") !=
                       std::string::npos,
               "runtime grid pass must build, validate, and upload the packed grid") &&
           requireTrue(
               reflectionPass.find(
                   ".readBuffer(resources.probes") != std::string::npos &&
                   reflectionPass.find(
                       ".readTexture(resources.probeSpecularPrefilter") !=
                       std::string::npos,
               "reflection graph must declare probe buffers and cubemap reads") &&
           requireTrue(
               reflectionShader.find(
                   "uniform samplerCubeArray uProbeSpecularPrefilter") !=
                       std::string::npos &&
                   reflectionShader.find(
                       "sampleReflectionProbeGrid(") != std::string::npos &&
                   reflectionShader.find(
                       "uReflectionDebugMode == 33") != std::string::npos &&
                   reflectionShader.find(
                       "uReflectionDebugMode == 34") != std::string::npos,
               "reflection shading must consume Box Projection and expose ID/weight debug") &&
           requireTrue(
               pipeline.find(
                   "m_reflectionProbeCapturePass->prepareFrame") !=
                       std::string::npos &&
                   pipeline.find(
                       "m_reflectionProbeCapturePass->addGraphPasses") !=
                       std::string::npos &&
                   pipeline.find(
                   "m_reflectionProbeGridPass->prepareGraphFrame") !=
                       std::string::npos &&
                   pipeline.find(
                       "m_reflectionProbeGridPass->importGraphResources") !=
                       std::string::npos &&
                   pipeline.find(
                       "m_reflectionProbeGridPass->addGraphPasses") !=
                       std::string::npos &&
                   pipeline.find(
                       "m_reflectionProbeGridPass->finishGraphExecution") !=
                       std::string::npos,
               "deferred graph must own the complete probe-grid transaction");
}

} // namespace

int main() {
    if (!testNormalizationAndPackedLayout() ||
        !testStructuredValidationFailures() ||
        !testInfluenceWeights() ||
        !testDeterministicTopFourSelection() ||
        !testDeterministicSpatialGrid() ||
        !testSpatialGridFailures() ||
        !testBoxProjection() || !testDeterministicCaptureWorkSchedule() ||
        !testCpuAndGlslMirror() ||
        !testRuntimeGridIntegrationContract()) {
        return 1;
    }
    std::cout << "[reflection_probe_contract_test] PASS\n";
    return 0;
}
