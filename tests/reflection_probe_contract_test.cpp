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
            source.find("reflectionProbeBoxProject") != std::string::npos,
        "GLSL must expose influence and box-projection reference functions");
}

} // namespace

int main() {
    if (!testNormalizationAndPackedLayout() ||
        !testStructuredValidationFailures() ||
        !testInfluenceWeights() ||
        !testDeterministicTopFourSelection() ||
        !testBoxProjection() || !testCpuAndGlslMirror()) {
        return 1;
    }
    std::cout << "[reflection_probe_contract_test] PASS\n";
    return 0;
}
