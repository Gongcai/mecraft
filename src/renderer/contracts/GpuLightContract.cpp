#include "GpuLightContract.h"

#include <glm/geometric.hpp>
#include <glm/matrix.hpp>

#include <cmath>

namespace renderer::contracts {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kHalfPi = kPi * 0.5f;

[[nodiscard]] bool finite(const glm::vec2& value) {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

[[nodiscard]] bool finite(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] GpuLightNormalizationResult fail(const GpuLightNormalizationError error, const GpuLightField field) {
    GpuLightNormalizationResult result;
    result.error = error;
    result.field = field;
    return result;
}

[[nodiscard]] bool validType(const GpuLightType type) {
    switch (type) {
    case GpuLightType::Directional:
    case GpuLightType::Point:
    case GpuLightType::Spot:
    case GpuLightType::Rect: return true;
    }
    return false;
}

[[nodiscard]] bool validUnit(const GpuLightIntensityUnit unit) {
    switch (unit) {
    case GpuLightIntensityUnit::Lux:
    case GpuLightIntensityUnit::Lumen:
    case GpuLightIntensityUnit::Candela:
    case GpuLightIntensityUnit::Nit: return true;
    }
    return false;
}

[[nodiscard]] bool validShadowPolicy(const GpuLightShadowPolicy policy) {
    switch (policy) {
    case GpuLightShadowPolicy::None:
    case GpuLightShadowPolicy::RasterDynamic:
    case GpuLightShadowPolicy::RasterCached:
    case GpuLightShadowPolicy::RayQuery: return true;
    }
    return false;
}

} // namespace

bool GpuLightNormalizationResult::succeeded() const {
    return error == GpuLightNormalizationError::None;
}

bool AnalyticLightInstantiationResult::succeeded() const {
    return error == AnalyticLightInstantiationError::None;
}

bool gpuLightPackedRangeValid(const GpuLight& light) {
    const uint32_t type = light.classificationAndIdentity.x;
    if (type > static_cast<uint32_t>(GpuLightType::Rect)) {
        return false;
    }
    const bool directional = type == static_cast<uint32_t>(GpuLightType::Directional);
    if (directional) {
        return light.positionAndRange.w == 0.0f && light.direction.w == 0.0f;
    }
    const float range = light.positionAndRange.w;
    const float inverseRangeSquared = light.direction.w;
    if (!std::isfinite(range) || range <= 0.0f || !std::isfinite(inverseRangeSquared) || inverseRangeSquared <= 0.0f) {
        return false;
    }
    const double normalizedRange =
        static_cast<double>(inverseRangeSquared) * static_cast<double>(range) * static_cast<double>(range);
    return std::isfinite(normalizedRange) && std::abs(normalizedRange - 1.0) <= 1.0e-4;
}

GpuLightNormalizationResult normalizeGpuLight(const GpuLightNormalizationInput& input) {
    if (!validType(input.type)) {
        return fail(GpuLightNormalizationError::InvalidType, GpuLightField::Type);
    }
    if (!input.lightId.isValid()) {
        return fail(GpuLightNormalizationError::InvalidStableId, GpuLightField::StableLightId);
    }
    if (!validUnit(input.intensityUnit)) {
        return fail(GpuLightNormalizationError::InvalidIntensityUnit, GpuLightField::IntensityUnit);
    }
    if (!finite(input.positionMeters)) {
        return fail(GpuLightNormalizationError::NonFiniteValue, GpuLightField::Position);
    }
    if (!finite(input.emissionDirection)) {
        return fail(GpuLightNormalizationError::NonFiniteValue, GpuLightField::Direction);
    }
    if (!std::isfinite(input.rangeMeters)) {
        return fail(GpuLightNormalizationError::NonFiniteValue, GpuLightField::Range);
    }
    if (!finite(input.colorLinear)) {
        return fail(GpuLightNormalizationError::NonFiniteValue, GpuLightField::Color);
    }
    if (!std::isfinite(input.intensity)) {
        return fail(GpuLightNormalizationError::NonFiniteValue, GpuLightField::Intensity);
    }
    if (!std::isfinite(input.innerConeAngleRadians)) {
        return fail(GpuLightNormalizationError::NonFiniteValue, GpuLightField::InnerConeAngle);
    }
    if (!std::isfinite(input.outerConeAngleRadians)) {
        return fail(GpuLightNormalizationError::NonFiniteValue, GpuLightField::OuterConeAngle);
    }
    if (!finite(input.rectSizeMeters)) {
        return fail(GpuLightNormalizationError::NonFiniteValue, GpuLightField::RectSize);
    }
    if (input.colorLinear.x < 0.0f || input.colorLinear.y < 0.0f || input.colorLinear.z < 0.0f) {
        return fail(GpuLightNormalizationError::ValueOutOfRange, GpuLightField::Color);
    }
    if (input.intensity < 0.0f) {
        return fail(GpuLightNormalizationError::ValueOutOfRange, GpuLightField::Intensity);
    }
    if ((input.contributionFlags & ~kGpuLightKnownContributionFlags) != 0u) {
        return fail(GpuLightNormalizationError::UnknownContributionFlags, GpuLightField::ContributionFlags);
    }
    if (!validShadowPolicy(input.shadowPolicy)) {
        return fail(GpuLightNormalizationError::InvalidShadowPolicy, GpuLightField::ShadowPolicy);
    }
    if ((input.shadowPolicy == GpuLightShadowPolicy::None) != (input.shadowIndex == kGpuLightInvalidResourceIndex)) {
        return fail(GpuLightNormalizationError::ShadowIndexConflict, GpuLightField::ShadowIndex);
    }

    const bool directional = input.type == GpuLightType::Directional;
    const bool point = input.type == GpuLightType::Point;
    const bool spot = input.type == GpuLightType::Spot;
    const bool rect = input.type == GpuLightType::Rect;

    if ((directional && input.intensityUnit != GpuLightIntensityUnit::Lux) ||
        (rect && input.intensityUnit != GpuLightIntensityUnit::Nit) ||
        ((point || spot) && input.intensityUnit != GpuLightIntensityUnit::Lumen &&
         input.intensityUnit != GpuLightIntensityUnit::Candela)) {
        return fail(GpuLightNormalizationError::InvalidIntensityUnit, GpuLightField::IntensityUnit);
    }
    if (directional) {
        if (input.positionMeters != glm::vec3(0.0f)) {
            return fail(GpuLightNormalizationError::ValueOutOfRange, GpuLightField::Position);
        }
        if (input.rangeMeters != 0.0f) {
            return fail(GpuLightNormalizationError::ValueOutOfRange, GpuLightField::Range);
        }
    } else if (input.rangeMeters <= 0.0f) {
        return fail(GpuLightNormalizationError::ValueOutOfRange, GpuLightField::Range);
    }
    float inverseRangeSquared = 0.0f;
    if (!directional) {
        inverseRangeSquared = 1.0f / (input.rangeMeters * input.rangeMeters);
        if (!std::isfinite(inverseRangeSquared) || inverseRangeSquared <= 0.0f) {
            return fail(GpuLightNormalizationError::ValueOutOfRange, GpuLightField::Range);
        }
    }

    glm::vec3 normalizedDirection{0.0f};
    if (point) {
        if (input.emissionDirection != glm::vec3(0.0f)) {
            return fail(GpuLightNormalizationError::InvalidDirection, GpuLightField::Direction);
        }
    } else {
        const float directionLengthSquared = glm::dot(input.emissionDirection, input.emissionDirection);
        if (directionLengthSquared <= 0.0f) {
            return fail(GpuLightNormalizationError::InvalidDirection, GpuLightField::Direction);
        }
        normalizedDirection = input.emissionDirection / std::sqrt(directionLengthSquared);
    }

    if (spot) {
        if (input.innerConeAngleRadians < 0.0f || input.outerConeAngleRadians <= input.innerConeAngleRadians ||
            input.outerConeAngleRadians > kHalfPi) {
            return fail(GpuLightNormalizationError::InvalidSpotCone, GpuLightField::OuterConeAngle);
        }
    } else if (input.innerConeAngleRadians != 0.0f || input.outerConeAngleRadians != 0.0f) {
        return fail(GpuLightNormalizationError::InvalidSpotCone, GpuLightField::InnerConeAngle);
    }

    if (rect) {
        if (input.rectSizeMeters.x <= 0.0f || input.rectSizeMeters.y <= 0.0f) {
            return fail(GpuLightNormalizationError::InvalidRectSize, GpuLightField::RectSize);
        }
    } else if (input.rectSizeMeters != glm::vec2(0.0f)) {
        return fail(GpuLightNormalizationError::InvalidRectSize, GpuLightField::RectSize);
    }

    float shadingIntensity = input.intensity;
    if (input.intensityUnit == GpuLightIntensityUnit::Lumen) {
        if (point) {
            shadingIntensity = input.intensity / (4.0f * kPi);
        } else {
            const float solidAngle = 2.0f * kPi * (1.0f - std::cos(input.outerConeAngleRadians));
            shadingIntensity = input.intensity / solidAngle;
        }
    }

    GpuLightNormalizationResult result;
    result.light.positionAndRange = directional ? glm::vec4(0.0f) : glm::vec4(input.positionMeters, input.rangeMeters);
    result.light.direction = glm::vec4(normalizedDirection, inverseRangeSquared);
    result.light.colorAndIntensity = glm::vec4(input.colorLinear, shadingIntensity);
    result.light.spotCosinesAndRectSize = {spot ? std::cos(input.innerConeAngleRadians) : 0.0f,
                                           spot ? std::cos(input.outerConeAngleRadians) : 0.0f,
                                           rect ? input.rectSizeMeters.x : 0.0f, rect ? input.rectSizeMeters.y : 0.0f};
    result.light.classificationAndIdentity = {static_cast<uint32_t>(input.type), input.lightId.value,
                                              static_cast<uint32_t>(input.shadowPolicy), input.shadowIndex};
    result.light.resourcesAndFlags = {input.cookieIndex, input.iesProfileIndex, input.contributionFlags,
                                      kGpuLightContractVersion};
    return result;
}

AnalyticLightInstantiationResult instantiateAnalyticLight(const AnalyticLightSourceDefinition& source,
                                                          const StableLightId lightId, const glm::mat4& localToWorld,
                                                          const glm::vec3& cameraPositionMeters) {
    AnalyticLightInstantiationResult result;
    if (!validShadowPolicy(source.shadowPolicy)) {
        result.error = AnalyticLightInstantiationError::NormalizationFailed;
        result.normalizationError = GpuLightNormalizationError::InvalidShadowPolicy;
        result.normalizationField = GpuLightField::ShadowPolicy;
        return result;
    }
    if (!finite(cameraPositionMeters)) {
        result.error = AnalyticLightInstantiationError::InvalidCameraPosition;
        return result;
    }
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            if (!std::isfinite(localToWorld[column][row])) {
                result.error = AnalyticLightInstantiationError::NonFiniteTransform;
                return result;
            }
        }
    }
    constexpr float kTransformTolerance = 1.0e-5f;
    if (std::abs(localToWorld[0][3]) > kTransformTolerance || std::abs(localToWorld[1][3]) > kTransformTolerance ||
        std::abs(localToWorld[2][3]) > kTransformTolerance ||
        std::abs(localToWorld[3][3] - 1.0f) > kTransformTolerance) {
        result.error = AnalyticLightInstantiationError::NonAffineTransform;
        return result;
    }

    glm::vec3 basis[3] = {glm::vec3(localToWorld[0]), glm::vec3(localToWorld[1]), glm::vec3(localToWorld[2])};
    for (glm::vec3& axis : basis) {
        const float lengthSquared = glm::dot(axis, axis);
        if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-12f) {
            result.error = AnalyticLightInstantiationError::DegenerateTransformBasis;
            return result;
        }
        axis /= std::sqrt(lengthSquared);
    }
    constexpr float kOrthogonalityTolerance = 1.0e-4f;
    if (std::abs(glm::dot(basis[0], basis[1])) > kOrthogonalityTolerance ||
        std::abs(glm::dot(basis[0], basis[2])) > kOrthogonalityTolerance ||
        std::abs(glm::dot(basis[1], basis[2])) > kOrthogonalityTolerance) {
        result.error = AnalyticLightInstantiationError::ShearedTransform;
        return result;
    }

    const glm::mat3 orientation(basis[0], basis[1], basis[2]);
    const bool directional = source.type == GpuLightType::Directional;
    GpuLightNormalizationInput input;
    input.lightId = lightId;
    input.type = source.type;
    input.positionMeters =
        directional ? glm::vec3(0.0f)
                    : glm::vec3(localToWorld * glm::vec4(source.localPositionMeters, 1.0f)) - cameraPositionMeters;
    input.emissionDirection = orientation * source.localEmissionDirection;
    input.rangeMeters = source.rangeMeters;
    input.colorLinear = source.colorLinear;
    input.intensity = source.intensity;
    input.intensityUnit = source.intensityUnit;
    input.innerConeAngleRadians = source.innerConeAngleRadians;
    input.outerConeAngleRadians = source.outerConeAngleRadians;
    input.rectSizeMeters = source.rectSizeMeters;
    input.contributionFlags = source.contributionFlags;
    const GpuLightNormalizationResult normalized = normalizeGpuLight(input);
    if (!normalized.succeeded()) {
        result.error = AnalyticLightInstantiationError::NormalizationFailed;
        result.normalizationError = normalized.error;
        result.normalizationField = normalized.field;
        return result;
    }
    result.sceneLight.light = normalized.light;
    result.sceneLight.requestedShadowPolicy = source.shadowPolicy;
    return result;
}

const char* gpuLightNormalizationErrorStableId(const GpuLightNormalizationError error) {
    switch (error) {
    case GpuLightNormalizationError::None: return "None";
    case GpuLightNormalizationError::InvalidType: return "InvalidType";
    case GpuLightNormalizationError::InvalidStableId: return "InvalidStableId";
    case GpuLightNormalizationError::InvalidIntensityUnit: return "InvalidIntensityUnit";
    case GpuLightNormalizationError::NonFiniteValue: return "NonFiniteValue";
    case GpuLightNormalizationError::ValueOutOfRange: return "ValueOutOfRange";
    case GpuLightNormalizationError::InvalidDirection: return "InvalidDirection";
    case GpuLightNormalizationError::InvalidSpotCone: return "InvalidSpotCone";
    case GpuLightNormalizationError::InvalidRectSize: return "InvalidRectSize";
    case GpuLightNormalizationError::InvalidShadowPolicy: return "InvalidShadowPolicy";
    case GpuLightNormalizationError::ShadowIndexConflict: return "ShadowIndexConflict";
    case GpuLightNormalizationError::UnknownContributionFlags: return "UnknownContributionFlags";
    }
    return "InvalidGpuLightNormalizationError";
}

const char* gpuLightFieldStableId(const GpuLightField field) {
    switch (field) {
    case GpuLightField::None: return "None";
    case GpuLightField::Type: return "Type";
    case GpuLightField::StableLightId: return "StableLightId";
    case GpuLightField::Position: return "Position";
    case GpuLightField::Direction: return "Direction";
    case GpuLightField::Range: return "Range";
    case GpuLightField::Color: return "Color";
    case GpuLightField::Intensity: return "Intensity";
    case GpuLightField::IntensityUnit: return "IntensityUnit";
    case GpuLightField::InnerConeAngle: return "InnerConeAngle";
    case GpuLightField::OuterConeAngle: return "OuterConeAngle";
    case GpuLightField::RectSize: return "RectSize";
    case GpuLightField::ShadowPolicy: return "ShadowPolicy";
    case GpuLightField::ShadowIndex: return "ShadowIndex";
    case GpuLightField::ContributionFlags: return "ContributionFlags";
    }
    return "InvalidGpuLightField";
}

const char* analyticLightInstantiationErrorStableId(const AnalyticLightInstantiationError error) {
    switch (error) {
    case AnalyticLightInstantiationError::None: return "None";
    case AnalyticLightInstantiationError::InvalidCameraPosition: return "InvalidCameraPosition";
    case AnalyticLightInstantiationError::NonFiniteTransform: return "NonFiniteTransform";
    case AnalyticLightInstantiationError::NonAffineTransform: return "NonAffineTransform";
    case AnalyticLightInstantiationError::DegenerateTransformBasis: return "DegenerateTransformBasis";
    case AnalyticLightInstantiationError::ShearedTransform: return "ShearedTransform";
    case AnalyticLightInstantiationError::NormalizationFailed: return "NormalizationFailed";
    }
    return "InvalidAnalyticLightInstantiationError";
}

} // namespace renderer::contracts
