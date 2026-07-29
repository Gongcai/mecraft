#include "renderer/contracts/GpuLightContract.h"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <type_traits>

namespace {

bool requireTrue(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[gpu_light_contract_test] FAIL: " << message << '\n';
        return false;
    }
    return true;
}

bool near(const float actual, const float expected, const float tolerance) {
    return std::abs(actual - expected) <= tolerance;
}

bool testStableIdentityAndLayout() {
    using namespace renderer::contracts;

    static_assert(!std::is_same_v<StableLightId, StableObjectId>);
    static_assert(!std::is_convertible_v<StableLightId, StableObjectId>);
    static_assert(offsetof(GpuLight, positionAndRange) == 0u);
    static_assert(offsetof(GpuLight, direction) == 16u);
    static_assert(offsetof(GpuLight, colorAndIntensity) == 32u);
    static_assert(offsetof(GpuLight, spotCosinesAndRectSize) == 48u);
    static_assert(offsetof(GpuLight, classificationAndIdentity) == 64u);
    static_assert(offsetof(GpuLight, resourcesAndFlags) == 80u);

    const auto first = allocateStableSceneId<StableLightIdTag>();
    const auto second = allocateStableSceneId<StableLightIdTag>();
    return requireTrue(first.has_value() && second.has_value(),
                       "stable light IDs must allocate explicitly") &&
           requireTrue(first->isValid() && second->isValid() && *first != *second,
                       "stable light IDs must be non-zero and unique");
}

bool testPhysicalUnitNormalization() {
    using namespace renderer::contracts;

    GpuLightNormalizationInput point;
    point.lightId = StableLightId{41u};
    point.type = GpuLightType::Point;
    point.positionMeters = {1.0f, 2.0f, 3.0f};
    point.rangeMeters = 12.0f;
    point.colorLinear = {1.0f, 0.5f, 0.25f};
    point.intensity = 1256.6370614f;
    point.intensityUnit = GpuLightIntensityUnit::Lumen;
    point.shadowPolicy = GpuLightShadowPolicy::Dynamic;
    point.shadowIndex = 7u;
    point.cookieIndex = 3u;
    point.iesProfileIndex = 5u;
    point.contributionFlags =
        gpuLightContributionFlagBit(GpuLightContributionFlag::Diffuse) |
        gpuLightContributionFlagBit(GpuLightContributionFlag::Volumetric);
    const GpuLightNormalizationResult pointResult = normalizeGpuLight(point);
    if (!requireTrue(pointResult.succeeded(),
                     "point lumens must normalize to a GPU light") ||
        !requireTrue(near(pointResult.light.colorAndIntensity.w, 100.0f, 1.0e-4f),
                     "isotropic point lumens must convert to candela") ||
        !requireTrue(pointResult.light.positionAndRange == glm::vec4(1.0f, 2.0f, 3.0f, 12.0f),
                     "point position and range must preserve meters") ||
        !requireTrue(pointResult.light.classificationAndIdentity ==
                         glm::uvec4(1u, 41u, 1u, 7u),
                     "classification and stable identity must use fixed fields") ||
        !requireTrue(pointResult.light.resourcesAndFlags ==
                         glm::uvec4(3u, 5u, point.contributionFlags,
                                    kGpuLightContractVersion),
                     "resource indices and flags must use fixed fields")) {
        return false;
    }

    GpuLightNormalizationInput spot;
    spot.lightId = StableLightId{42u};
    spot.type = GpuLightType::Spot;
    spot.positionMeters = {4.0f, 5.0f, 6.0f};
    spot.emissionDirection = {0.0f, 2.0f, 0.0f};
    spot.rangeMeters = 20.0f;
    spot.intensity = 1000.0f;
    spot.intensityUnit = GpuLightIntensityUnit::Lumen;
    spot.innerConeAngleRadians = 0.25f;
    spot.outerConeAngleRadians = 0.5f;
    const GpuLightNormalizationResult spotResult = normalizeGpuLight(spot);
    const float expectedSpotCandela =
        1000.0f / (2.0f * 3.14159265358979323846f * (1.0f - std::cos(0.5f)));
    if (!requireTrue(spotResult.succeeded(),
                     "spot lumens must normalize to a GPU light") ||
        !requireTrue(near(spotResult.light.colorAndIntensity.w,
                          expectedSpotCandela, 1.0e-3f),
                     "spot lumens must use the outer-cone solid angle") ||
        !requireTrue(spotResult.light.direction == glm::vec4(0.0f, 1.0f, 0.0f, 0.0f),
                     "directional inputs must be normalized once") ||
        !requireTrue(near(spotResult.light.spotCosinesAndRectSize.x,
                          std::cos(0.25f), 1.0e-6f) &&
                         near(spotResult.light.spotCosinesAndRectSize.y,
                              std::cos(0.5f), 1.0e-6f),
                     "spot angles must be packed as inner and outer cosines")) {
        return false;
    }

    GpuLightNormalizationInput directional;
    directional.lightId = StableLightId{43u};
    directional.type = GpuLightType::Directional;
    directional.emissionDirection = {1.0f, 2.0f, 2.0f};
    directional.intensity = 120000.0f;
    directional.intensityUnit = GpuLightIntensityUnit::Lux;
    const GpuLightNormalizationResult directionalResult =
        normalizeGpuLight(directional);
    if (!requireTrue(directionalResult.succeeded(),
                     "directional lux must remain directly usable") ||
        !requireTrue(directionalResult.light.positionAndRange == glm::vec4(0.0f),
                     "directional lights must not carry finite bounds") ||
        !requireTrue(near(glm::length(glm::vec3(directionalResult.light.direction)),
                          1.0f, 1.0e-6f),
                     "directional vectors must be unit length") ||
        !requireTrue(directionalResult.light.colorAndIntensity.w == 120000.0f,
                     "directional intensity must remain in lux")) {
        return false;
    }

    GpuLightNormalizationInput rect;
    rect.lightId = StableLightId{44u};
    rect.type = GpuLightType::Rect;
    rect.positionMeters = {2.0f, 4.0f, 8.0f};
    rect.emissionDirection = {0.0f, 0.0f, -1.0f};
    rect.rangeMeters = 30.0f;
    rect.intensity = 250.0f;
    rect.intensityUnit = GpuLightIntensityUnit::Nit;
    rect.rectSizeMeters = {3.0f, 2.0f};
    const GpuLightNormalizationResult rectResult = normalizeGpuLight(rect);
    return requireTrue(rectResult.succeeded(),
                       "Rect nit values must normalize to a GPU light") &&
           requireTrue(rectResult.light.spotCosinesAndRectSize ==
                           glm::vec4(0.0f, 0.0f, 3.0f, 2.0f),
                       "Rect dimensions must preserve meters") &&
           requireTrue(rectResult.light.colorAndIntensity.w == 250.0f,
                       "Rect intensity must remain in nit");
}

bool testStructuredErrors() {
    using namespace renderer::contracts;

    GpuLightNormalizationInput input;
    input.type = GpuLightType::Point;
    input.rangeMeters = 10.0f;
    input.intensity = 100.0f;

    GpuLightNormalizationResult result = normalizeGpuLight(input);
    if (!requireTrue(result.error == GpuLightNormalizationError::InvalidStableId &&
                         result.field == GpuLightField::StableLightId,
                     "zero stable IDs must fail explicitly") ||
        !requireTrue(std::string(gpuLightNormalizationErrorStableId(result.error)) ==
                         "InvalidStableId",
                     "light errors must expose stable identifiers")) {
        return false;
    }

    input.lightId = StableLightId{1u};
    input.colorLinear.x = std::numeric_limits<float>::quiet_NaN();
    result = normalizeGpuLight(input);
    if (!requireTrue(result.error == GpuLightNormalizationError::NonFiniteValue &&
                         result.field == GpuLightField::Color,
                     "non-finite colors must identify their field")) {
        return false;
    }

    input.colorLinear = {1.0f, 1.0f, 1.0f};
    input.shadowIndex = 0u;
    result = normalizeGpuLight(input);
    if (!requireTrue(result.error == GpuLightNormalizationError::ShadowIndexConflict,
                     "shadow allocations must agree with their policy")) {
        return false;
    }

    input.shadowIndex = kGpuLightInvalidResourceIndex;
    input.contributionFlags = 1u << 31u;
    result = normalizeGpuLight(input);
    if (!requireTrue(result.error == GpuLightNormalizationError::UnknownContributionFlags,
                     "unknown contribution bits must be rejected")) {
        return false;
    }

    input.contributionFlags = gpuLightContributionFlagBit(
        GpuLightContributionFlag::Diffuse);
    input.type = GpuLightType::Spot;
    input.emissionDirection = {0.0f, -1.0f, 0.0f};
    input.innerConeAngleRadians = 0.5f;
    input.outerConeAngleRadians = 0.25f;
    result = normalizeGpuLight(input);
    if (!requireTrue(result.error == GpuLightNormalizationError::InvalidSpotCone,
                     "reversed spot cones must fail explicitly")) {
        return false;
    }

    input.type = GpuLightType::Rect;
    input.intensityUnit = GpuLightIntensityUnit::Nit;
    input.innerConeAngleRadians = 0.0f;
    input.outerConeAngleRadians = 0.0f;
    result = normalizeGpuLight(input);
    return requireTrue(result.error == GpuLightNormalizationError::InvalidRectSize &&
                           result.field == GpuLightField::RectSize,
                       "non-positive Rect dimensions must fail explicitly");
}

bool testAnalyticLightInstantiation() {
    using namespace renderer::contracts;

    AnalyticLightSourceDefinition source;
    source.type = GpuLightType::Spot;
    source.localPositionMeters = {1.0f, 0.0f, 0.0f};
    source.localEmissionDirection = {0.0f, 0.0f, -1.0f};
    source.rangeMeters = 9.0f;
    source.colorLinear = {0.2f, 0.4f, 1.0f};
    source.intensity = 300.0f;
    source.intensityUnit = GpuLightIntensityUnit::Candela;
    source.innerConeAngleRadians = 0.2f;
    source.outerConeAngleRadians = 0.6f;

    const glm::mat4 model =
        glm::translate(glm::mat4(1.0f), glm::vec3(10.0f, 2.0f, -5.0f)) *
        glm::rotate(glm::mat4(1.0f), glm::radians(90.0f),
                    glm::vec3(0.0f, 1.0f, 0.0f)) *
        glm::scale(glm::mat4(1.0f), glm::vec3(2.0f, 3.0f, 4.0f));
    const glm::vec3 camera{1.0f, 1.0f, -1.0f};
    const AnalyticLightInstantiationResult instantiated =
        instantiateAnalyticLight(
            source, StableLightId{71u}, model, camera);
    const glm::vec3 expectedPosition =
        glm::vec3(model * glm::vec4(source.localPositionMeters, 1.0f)) -
        camera;
    const glm::mat3 orientation(
        glm::normalize(glm::vec3(model[0])),
        glm::normalize(glm::vec3(model[1])),
        glm::normalize(glm::vec3(model[2])));
    const glm::vec3 expectedDirection =
        glm::normalize(orientation * source.localEmissionDirection);
    if (!requireTrue(instantiated.succeeded(),
                     "orthogonal non-uniform instance transforms must remain valid") ||
        !requireTrue(glm::length(
                         glm::vec3(instantiated.light.positionAndRange) -
                         expectedPosition) < 1.0e-5f,
                     "analytic lights must upload camera-relative positions") ||
        !requireTrue(glm::length(
                         glm::vec3(instantiated.light.direction) -
                         expectedDirection) < 1.0e-5f,
                     "analytic light direction must exclude instance scale") ||
        !requireTrue(instantiated.light.positionAndRange.w == 9.0f,
                     "physical light range must not inherit scene scale")) {
        return false;
    }

    glm::mat4 sheared(1.0f);
    sheared[1][0] = 0.5f;
    const AnalyticLightInstantiationResult invalidTransform =
        instantiateAnalyticLight(
            source, StableLightId{72u}, sheared, glm::vec3(0.0f));
    if (!requireTrue(
            invalidTransform.error ==
                AnalyticLightInstantiationError::ShearedTransform,
            "sheared light transforms must fail explicitly") ||
        !requireTrue(
            std::string(analyticLightInstantiationErrorStableId(
                invalidTransform.error)) == "ShearedTransform",
            "analytic transform failures must expose stable identifiers")) {
        return false;
    }

    source.rangeMeters = 0.0f;
    const AnalyticLightInstantiationResult invalidSource =
        instantiateAnalyticLight(
            source, StableLightId{73u}, glm::mat4(1.0f),
            glm::vec3(0.0f));
    return requireTrue(
               invalidSource.error ==
                   AnalyticLightInstantiationError::NormalizationFailed &&
               invalidSource.normalizationError ==
                   GpuLightNormalizationError::ValueOutOfRange &&
               invalidSource.normalizationField == GpuLightField::Range,
               "invalid physical source values must preserve normalization diagnostics");
}

bool testShaderLayoutMirror() {
    const std::string shaderPath =
        std::string(MECRAFT_TEST_SOURCE_DIR) +
        "/assets/shaders/gpu_light_contract.glsl";
    std::ifstream stream(shaderPath, std::ios::binary);
    if (!requireTrue(stream.is_open(),
                     "GPU light GLSL contract must be readable")) {
        return false;
    }
    const std::string source((std::istreambuf_iterator<char>(stream)),
                             std::istreambuf_iterator<char>());
    if (!requireTrue(source.find("GPU_LIGHT_CONTRACT_VERSION = 1u") !=
                         std::string::npos,
                     "GLSL light contract must mirror the CPU version") ||
        !requireTrue(source.find("GPU_LIGHT_TYPE_RECT = 3u") !=
                         std::string::npos,
                     "GLSL light types must mirror the CPU order") ||
        !requireTrue(source.find("GPU_LIGHT_CONTRIBUTION_VOLUMETRIC") !=
                         std::string::npos,
                     "GLSL flags must mirror all contribution channels")) {
        return false;
    }
    constexpr std::array<const char*, 6> kOrderedFields{
        {"vec4 positionAndRange;", "vec4 direction;",
         "vec4 colorAndIntensity;", "vec4 spotCosinesAndRectSize;",
         "uvec4 classificationAndIdentity;", "uvec4 resourcesAndFlags;"}};
    size_t offset = 0u;
    for (const char* field : kOrderedFields) {
        const size_t found = source.find(field, offset);
        if (!requireTrue(found != std::string::npos,
                         "GLSL light layout must mirror every CPU field")) {
            return false;
        }
        offset = found + std::string(field).size();
    }
    return true;
}

} // namespace

int main() {
    if (!testStableIdentityAndLayout() || !testPhysicalUnitNormalization() ||
        !testStructuredErrors() || !testAnalyticLightInstantiation() ||
        !testShaderLayoutMirror()) {
        return 1;
    }
    std::cout << "[gpu_light_contract_test] PASS\n";
    return 0;
}
