#include "renderer/contracts/RtgiNrdSignalContract.h"

#include <algorithm>
#include <cmath>

namespace renderer::contracts {
namespace {
[[nodiscard]] bool finite(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool validRadiance(const glm::vec3& radiance) {
    return finite(radiance) && radiance.x >= 0.0f && radiance.y >= 0.0f && radiance.z >= 0.0f;
}

[[nodiscard]] glm::vec3 clampToFp16(const glm::vec3& value) {
    return {std::min(value.x, kRtgiNrdFp16Max), std::min(value.y, kRtgiNrdFp16Max), std::min(value.z, kRtgiNrdFp16Max)};
}
} // namespace

bool rtgiReblurHitDistanceParametersValid(const RtgiReblurHitDistanceParameters& parameters) {
    return std::isfinite(parameters.constantScale) && parameters.constantScale > 0.0f &&
           std::isfinite(parameters.viewZScale) && parameters.viewZScale > 0.0f &&
           std::isfinite(parameters.roughnessScale) && parameters.roughnessScale >= 1.0f;
}

std::optional<glm::vec3> rtgiRemovePreExposure(const glm::vec3& radiance, const float preExposure) {
    if (!validRadiance(radiance) || !std::isfinite(preExposure) || preExposure <= 0.0f) {
        return std::nullopt;
    }
    const glm::vec3 sceneRadiance = radiance / preExposure;
    return validRadiance(sceneRadiance) ? std::optional(sceneRadiance) : std::nullopt;
}

std::optional<glm::vec3> rtgiApplyPreExposure(const glm::vec3& radiance, const float preExposure) {
    if (!validRadiance(radiance) || !std::isfinite(preExposure) || preExposure <= 0.0f) {
        return std::nullopt;
    }
    const glm::vec3 preExposedRadiance = radiance * preExposure;
    return validRadiance(preExposedRadiance) ? std::optional(preExposedRadiance) : std::nullopt;
}

std::optional<float> rtgiReblurNormalizedHitDistance(const float hitDistance, const float viewZ,
                                                     const RtgiReblurHitDistanceParameters& parameters,
                                                     const float roughness) {
    if (!std::isfinite(hitDistance) || hitDistance < 0.0f || !std::isfinite(viewZ) ||
        !rtgiReblurHitDistanceParametersValid(parameters) || !std::isfinite(roughness) || roughness < 0.0f ||
        roughness > 1.0f) {
        return std::nullopt;
    }

    const float magicCurve =
        (1.0f - std::exp2(-200.0f * roughness * roughness)) * std::sqrt(std::clamp(roughness, 0.0f, 1.0f));
    const float roughnessFactor = parameters.roughnessScale + (1.0f - parameters.roughnessScale) * magicCurve;
    const float normalizationScale =
        (parameters.constantScale + std::abs(viewZ) * parameters.viewZScale) * roughnessFactor;
    if (!std::isfinite(normalizationScale) || normalizationScale <= 0.0f) {
        return std::nullopt;
    }

    return std::max(std::clamp(hitDistance / normalizationScale, 0.0f, 1.0f), kRtgiNrdEpsilon);
}

std::optional<glm::vec4> rtgiPackRelaxRadianceAndHitDistance(const glm::vec3& radiance, const float hitDistance) {
    if (!validRadiance(radiance) || !std::isfinite(hitDistance) || hitDistance < 0.0f) {
        return std::nullopt;
    }
    return glm::vec4(clampToFp16(radiance), std::min(hitDistance, kRtgiNrdFp16Max));
}

std::optional<glm::vec4> rtgiPackReblurRadianceAndNormalizedHitDistance(const glm::vec3& radiance,
                                                                        const float normalizedHitDistance) {
    if (!validRadiance(radiance) || !std::isfinite(normalizedHitDistance) || normalizedHitDistance < 0.0f ||
        normalizedHitDistance > 1.0f) {
        return std::nullopt;
    }

    const glm::vec3 boundedRadiance = clampToFp16(radiance);
    const float luminance = boundedRadiance.x * 0.25f + boundedRadiance.y * 0.5f + boundedRadiance.z * 0.25f;
    const float orangeChroma = boundedRadiance.x * 0.5f - boundedRadiance.z * 0.5f;
    const float greenChroma = -boundedRadiance.x * 0.25f + boundedRadiance.y * 0.5f - boundedRadiance.z * 0.25f;
    return glm::vec4(luminance, orangeChroma, greenChroma, normalizedHitDistance);
}

} // namespace renderer::contracts
